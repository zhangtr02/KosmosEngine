#pragma once

#include "brdf.slang"

[[vk::binding(6, 0)]] Sampler2D prefilteredMap;
[[vk::binding(7, 0)]] Sampler2D brdfLut;
[[vk::binding(26, 0)]] Sampler2D prefilteredSheenMap;

struct Surface
{
    float3 normal;
    float3 view;
    float3 color;
    float3 dielectricF0;
    float dielectricF90;
    float metallic;
    float roughness;
    bool legacy;
    float3 tangent;
    float3 bitangent;
    float anisotropy;
    float3 coatNormal;
    float coat;
    float coatRoughness;
    float3 sheen;
    float sheenRoughness;
    float3 film; // Strength, IOR, thickness in nm.
    float transmission;
    float transmissionRoughness;
    float3 transmissionColor;
};

float maxComponent(float3 value)
{
    return max(value.x, max(value.y, value.z));
}

float3 safeNormal(float3 value, float3 fallback)
{
    const float squareLength = dot(value, value);
    return squareLength > 1e-12 ? value * rsqrt(squareLength) : fallback;
}

// RGB sensitivity fit from Belcour and Barla's Fourier expansion of thin-film interference.
float3 filmSensitivity(float opticalPath, float3 shift)
{
    const float phase = 2.0 * PI * opticalPath * 1e-9;
    const float phaseSquared = phase * phase;
    const float3 weights = float3(5.4856e-13, 4.4201e-13, 5.2481e-13);
    const float3 frequency = float3(1.6810e6, 1.7953e6, 2.2084e6);
    const float3 variance = float3(4.3278e9, 9.3046e9, 6.6121e9);
    float3 xyz = weights * sqrt(2.0 * PI * variance) * cos(frequency * phase + shift) *
                 exp(-phaseSquared * variance);
    xyz.x += 9.7470e-14 * sqrt(2.0 * PI * 4.5282e9) *
             cos(2.2399e6 * phase + shift.x) * exp(-4.5282e9 * phaseSquared);
    const float3x3 xyzToRgb = float3x3(3.2404542, -1.5371385, -0.4985314,
                                     -0.9692660, 1.8760108, 0.0415560,
                                     0.0556434, -0.2040259, 1.0572252);
    return mul(xyzToRgb, xyz / 1.0685e-7);
}

float3 filmReflectance(float cosAir, float ior, float thickness, float3 baseF0)
{
    const float filmIor = lerp(1.0, ior, smoothstep(0.0, 0.03, thickness));
    const float cosFilm = sqrt(saturate(1.0 - (1.0 - cosAir * cosAir) / (filmIor * filmIor)));
    const float ratio = (filmIor - 1.0) / (filmIor + 1.0);
    const float front = fresnelSchlick(cosAir, ratio * ratio).x;
    const float3 root = sqrt(clamp(baseF0, 0.0, 0.9999));
    const float3 baseIor = (1.0 + root) / (1.0 - root);
    const float3 backRatio = (baseIor - filmIor) / (baseIor + filmIor);
    const float3 back = fresnelSchlick(cosFilm, backRatio * backRatio);
    const float3 phase = PI * (2.0 - step(filmIor, baseIor));
    const float opticalPath = 2.0 * filmIor * thickness * cosFilm;
    const float3 product = clamp(front * back, 0.0, 0.9999);
    const float3 roundTrip = sqrt(product);
    const float transmission = 1.0 - front;
    const float3 internal = transmission * transmission * back / (1.0 - product);
    const float3 harmonic = (internal - transmission) * roundTrip;
    return saturate(front + internal + 2.0 * harmonic *
                    (filmSensitivity(opticalPath, phase) +
                     roundTrip * filmSensitivity(2.0 * opticalPath, 2.0 * phase)));
}

float3 surfaceFresnel(Surface surface, float cosine, out float diffuseWeight)
{
    const float grazing = pow(1.0 - cosine, 5.0);
    float3 dielectric = lerp(surface.dielectricF0, surface.dielectricF90, grazing);
    float3 metal = lerp(surface.color, 1.0, grazing);
    if (surface.film.x > 0.0)
    {
        if (surface.metallic < 1.0 && surface.dielectricF90 > 0.0)
            dielectric = lerp(dielectric, surface.dielectricF90 *
                filmReflectance(cosine, surface.film.y, surface.film.z,
                                surface.dielectricF0 / surface.dielectricF90), surface.film.x);
        if (surface.metallic > 0.0)
            metal = lerp(metal, filmReflectance(cosine, surface.film.y, surface.film.z,
                                               surface.color), surface.film.x);
    }
    diffuseWeight = surface.legacy ? 1.0 - maxComponent(surface.dielectricF0) :
                    (1.0 - maxComponent(dielectric)) * (1.0 - surface.metallic);
    return lerp(dielectric, metal, surface.metallic);
}

float specularLobe(Surface surface, float3 light, float3 halfVector, float nDotL, float nDotV)
{
    const float nDotH = saturate(dot(surface.normal, halfVector));
    const float alpha = surface.roughness * surface.roughness;
    const float alphaSquared = alpha * alpha;
    if (surface.anisotropy <= 0.0)
        return distributionGGX(nDotH, alphaSquared) * visibilitySmith(nDotL, nDotV, alphaSquared);

    const float alphaT = lerp(alpha, 1.0, surface.anisotropy * surface.anisotropy);
    const float alphaProduct = alpha * alphaT;
    const float3 h = float3(alpha * dot(surface.tangent, halfVector),
                           alphaT * dot(surface.bitangent, halfVector), alphaProduct * nDotH);
    const float distribution = pow(alphaProduct / max(dot(h, h), 1e-12), 2.0) * alphaProduct / PI;
    const float3 v = float3(alphaT * dot(surface.tangent, surface.view),
                           alpha * dot(surface.bitangent, surface.view), nDotV);
    const float3 l = float3(alphaT * dot(surface.tangent, light),
                           alpha * dot(surface.bitangent, light), nDotL);
    const float visibility = 0.5 / max(nDotL * length(v) + nDotV * length(l), 1e-6);
    return distribution * visibility;
}

float coatCoverage(Surface surface)
{
    return surface.coat * fresnelSchlick(saturate(dot(surface.coatNormal, surface.view)), 0.04).x;
}

float baseLayerWeight(Surface surface, float nDotV)
{
    const float sheen = maxComponent(surface.sheen);
    const float energy = sheen > 0.0
        ? brdfLut.SampleLevel(float2(nDotV, surface.sheenRoughness), 0).b : 0.0;
    return (1.0 - sheen * energy) * (1.0 - coatCoverage(surface));
}

// Direct BRDF multiplied by projected area. Lighting and shadow visibility are applied by the caller.
float3 directSurface(Surface surface, float3 light)
{
    const float3 halfVector = safeNormal(surface.view + light, surface.normal);
    const float vDotH = saturate(dot(surface.view, halfVector));
    const float nDotV = saturate(dot(surface.normal, surface.view));
    const float nDotL = saturate(dot(surface.normal, light));
    float3 result = 0.0;
    if (nDotL > 0.0 && nDotV > 0.0)
    {
        float diffuseWeight;
        const float3 fresnel = surfaceFresnel(surface, vDotH, diffuseWeight);
        result = surface.color * (diffuseWeight * (1.0 - surface.transmission) / PI) +
                 fresnel * specularLobe(surface, light, halfVector, nDotL, nDotV);
        if (maxComponent(surface.sheen) > 0.0)
        {
            const float2 angles = float2(nDotV, nDotL);
            const float energy = max(brdfLut.SampleLevel(float2(angles.x, surface.sheenRoughness), 0).b,
                                     brdfLut.SampleLevel(float2(angles.y, surface.sheenRoughness), 0).b);
            result *= 1.0 - maxComponent(surface.sheen) * energy;
            result += surface.sheen * distributionCharlie(saturate(dot(surface.normal, halfVector)),
                                                           surface.sheenRoughness) *
                      visibilityNeubelt(nDotL, nDotV);
        }
        result *= nDotL;
    }
    if (surface.coat > 0.0)
    {
        result *= 1.0 - coatCoverage(surface);
        const float cV = saturate(dot(surface.coatNormal, surface.view));
        const float cL = saturate(dot(surface.coatNormal, light));
        if (cV > 0.0 && cL > 0.0)
        {
            const float a = surface.coatRoughness * surface.coatRoughness;
            result += surface.coat * fresnelSchlick(vDotH, 0.04) * cL *
                      distributionGGX(saturate(dot(surface.coatNormal, halfVector)), a * a) *
                      visibilitySmith(cL, cV, a * a);
        }
    }
    if (surface.transmission > 0.0 && nDotV > 0.0)
    {
        const float opposite = -dot(surface.normal, light);
        if (opposite > 0.0)
        {
            const float3 throughHalf = safeNormal(surface.view + light +
                                                   2.0 * opposite * surface.normal, surface.normal);
            float diffuseWeight;
            surfaceFresnel(surface, saturate(dot(surface.view, throughHalf)), diffuseWeight);
            const float alpha = max(surface.transmissionRoughness, 0.045);
            const float alphaSquared = alpha * alpha * alpha * alpha;
            const float lobe = distributionGGX(saturate(dot(surface.normal, throughHalf)), alphaSquared) *
                               visibilitySmith(opposite, nDotV, alphaSquared);
            result += surface.transmissionColor *
                      (surface.transmission * diffuseWeight * opposite * lobe *
                       baseLayerWeight(surface, nDotV));
        }
    }
    return result;
}

float3 filteredReflection(Sampler2D environment, float3 direction, float roughness)
{
    uint width, height, levels;
    environment.GetDimensions(0, width, height, levels);
    return environment.SampleLevel(environmentUV(direction), roughness * float(levels - 1)).rgb;
}

float3 environmentSurface(Surface surface, float3 irradiance, float2 strengths)
{
    const float nDotV = saturate(dot(surface.normal, surface.view));
    float diffuseWeight;
    const float3 fresnel = surfaceFresnel(surface, nDotV, diffuseWeight);
    float3 result = irradiance * surface.color *
                    (diffuseWeight * (1.0 - surface.transmission) * strengths.x / PI);
    const float3 f90 = lerp(surface.dielectricF90, 1.0, surface.metallic);
    float3 f0 = lerp(surface.dielectricF0, surface.color, surface.metallic);
    if (surface.film.x > 0.0)
    {
        const float grazing = min(pow(1.0 - nDotV, 5.0), 0.9999);
        f0 = saturate((fresnel - grazing * f90) / (1.0 - grazing));
    }
    float3 reflectionNormal = surface.normal;
    if (surface.anisotropy > 0.0)
    {
        const float3 bent = safeNormal(cross(cross(surface.bitangent, surface.view),
                                            surface.bitangent), surface.normal);
        const float blend = pow(1.0 - surface.anisotropy * (1.0 - surface.roughness), 4.0);
        reflectionNormal = safeNormal(lerp(bent, surface.normal, blend), surface.normal);
    }
    const float3 reflection = reflect(-surface.view, reflectionNormal);
    const float2 brdf = brdfLut.SampleLevel(float2(nDotV, surface.roughness), 0).rg;
    if (nDotV > 0.0)
        result += filteredReflection(prefilteredMap, reflection, surface.roughness) *
                  (f0 * brdf.x + f90 * brdf.y) * strengths.y;
    if (nDotV > 0.0 && maxComponent(surface.sheen) > 0.0)
    {
        const float energy = brdfLut.SampleLevel(float2(nDotV, surface.sheenRoughness), 0).b;
        result = result * (1.0 - maxComponent(surface.sheen) * energy) +
                 surface.sheen * energy * strengths.y *
                 filteredReflection(prefilteredSheenMap, reflect(-surface.view, surface.normal),
                                    surface.sheenRoughness);
    }
    if (surface.coat > 0.0)
    {
        const float coatNdotV = saturate(dot(surface.coatNormal, surface.view));
        result *= 1.0 - coatCoverage(surface);
        if (coatNdotV > 0.0)
        {
            const float2 coatBrdf = brdfLut.SampleLevel(float2(coatNdotV, surface.coatRoughness), 0).rg;
            result += surface.coat * (0.04 * coatBrdf.x + coatBrdf.y) * strengths.y *
                      filteredReflection(prefilteredMap, reflect(-surface.view, surface.coatNormal),
                                         surface.coatRoughness);
        }
    }
    return result;
}
