#pragma once

[[vk::binding(0, 2)]] Sampler2D transmissionBackground;
[[vk::binding(24, 0)]] Sampler2D transmissionMap;
[[vk::binding(25, 0)]] Sampler2D thicknessMap;

float transmissionRoughness(float roughness, float ior)
{
    return roughness * saturate(2.0 * (ior - 1.0));
}

float3 volumeAttenuation(float4 attenuation, float distance)
{
    if (distance <= 0.0 || attenuation.w <= 0.0)
        return 1.0;
    // Beer-Lambert law, using the material's transmittance at attenuation.w.
    return pow(saturate(attenuation.rgb), distance / attenuation.w);
}

float3 volumeRay(Surface surface, float3x3 meshFromWorld, float thickness, float ior)
{
    const float3 direction = refract(-surface.view, surface.normal, 1.0 / ior);
    const float localLength = length(mul(meshFromWorld, direction));
    return localLength > 1e-8 ? direction * (thickness / localLength) : 0.0;
}

float3 sampleRefraction(float3 position, float3 ray, float2 screen, float lod)
{
    const float4 clip = mul(scene.viewProjection, float4(position + ray, 1.0));
    const float2 uv = clip.w > 1e-6 ? 0.5 * (clip.xy / clip.w) + 0.5 : screen;
    return transmissionBackground.SampleLevel(uv, lod).rgb;
}

float3 transmittedScene(Surface surface, float3 position, float2 pixel, float3x3 meshFromWorld,
                        float3 optical, float4 attenuation)
{
    uint width, height, levels;
    transmissionBackground.GetDimensions(0, width, height, levels);
    const float2 screen = pixel / float2(width, height);
    const float thickness = optical.x, ior = optical.y, dispersion = optical.z;
    const float mipCount = float(levels - 1);
    if (thickness == 0.0 || dispersion == 0.0)
    {
        const float3 ray = volumeRay(surface, meshFromWorld, thickness, ior);
        return sampleRefraction(position, ray, screen, surface.transmissionRoughness * mipCount) *
               volumeAttenuation(attenuation, length(ray));
    }

    // Cauchy's equation evaluated at the Fraunhofer C, d, and F wavelengths.
    const float3 wavelength = float3(656.27, 587.56, 486.13);
    const float3 indices = max(ior + ((ior - 1.0) * dispersion / 20.0) *
                                   (523655.0 / (wavelength * wavelength) - 1.5168), 1.0);
    float3 color;
    [unroll]
    for (uint channel = 0; channel < 3; ++channel)
    {
        const float3 ray = volumeRay(surface, meshFromWorld, thickness, indices[channel]);
        const float lod = transmissionRoughness(surface.roughness, indices[channel]) * mipCount;
        const float3 sample = sampleRefraction(position, ray, screen, lod);
        color[channel] = sample[channel] * volumeAttenuation(attenuation, length(ray))[channel];
    }
    return color;
}
