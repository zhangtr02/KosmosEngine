#include "scene.slang"
#include "brdf.slang"
#include "material.slang"

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float3 normal : NORMAL;
    [[vk::location(3)]] float4 tangent : TANGENT;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
    float3 worldPosition : TEXCOORD2;
    float4 worldTangent : TEXCOORD3;
};

[[vk::binding(2, 0)]] Sampler2D normalMap;
[[vk::binding(3, 0)]] Sampler2D metallicRoughnessMap;
[[vk::binding(5, 0)]] Sampler2D irradianceMap;
[[vk::binding(6, 0)]] Sampler2D prefilteredMap;
[[vk::binding(7, 0)]] Sampler2D brdfLut;
[[vk::binding(8, 0)]] Sampler2D occlusionMap;
[[vk::binding(9, 0)]] Sampler2D shadowMap;
[[vk::binding(10, 0)]] Sampler2D emissiveMap;
[[vk::binding(11, 0)]] SamplerCube pointShadowMap;
[[vk::binding(12, 0)]] Sampler2D spotShadowMap;

float pointShadowCompare(float3 direction, float depth)
{
    float closest;
    if (scene.spotCone.z != 0.0)
    {
        // A direction (w = 0) removes view translation; XY/W gives its shadow-map UV.
        const float4 projected = mul(scene.spotViewProjection, float4(direction, 0.0));
        if (projected.w <= 0.0)
            return 1.0;
        const float2 uv = projected.xy / projected.w * 0.5 + 0.5;
        if (any(uv < 0.0) || any(uv > 1.0))
            return 1.0;
        closest = spotShadowMap.SampleLevel(uv, 0.0).r;
    }
    else
        closest = pointShadowMap.SampleLevel(direction, 0.0).r;
    closest *= scene.pointPositionRange.w;
    return depth <= closest ? 1.0 : 0.0;
}

float pointShadowVisibility(float3 fromLight, float distance, float nDotL, float3 geometricNormal)
{
    if (scene.pointShadowParams.x == 0.0)
        return 1.0;
    const float bias = scene.pointShadowParams.y * (2.0 - nDotL);
    const float depth = distance - bias;
    if (scene.pointShadowParams.z == 0.0 || scene.pointShadowParams.w <= 0.0)
        return pointShadowCompare(fromLight, depth);
    const float3 direction = fromLight / distance;
    const float3 up = abs(direction.z) < 0.999 ? float3(0, 0, 1) : float3(0, 1, 0);
    const float3 tangent = normalize(cross(up, direction));
    const float3 bitangent = cross(direction, tangent);
    uint width, height, levels;
    if (scene.spotCone.z != 0.0)
        spotShadowMap.GetDimensions(0, width, height, levels);
    else
        pointShadowMap.GetDimensions(0, width, height, levels);
    const float radius = 2.0 * scene.pointShadowParams.w / float(width) *
                         (scene.spotCone.z != 0.0 ? scene.spotCone.w : 1.0);
    // Center + eight disk directions; sample vectors can cross cube face boundaries.
    const float2 offsets[9] = {float2(0, 0), float2(1, 0), float2(-1, 0),
        float2(0, 1), float2(0, -1), float2(0.70710678, 0.70710678),
        float2(-0.70710678, 0.70710678), float2(0.70710678, -0.70710678),
        float2(-0.70710678, -0.70710678)};
    float visibility = 0.0;
    const float plane = dot(geometricNormal, direction);
    for (uint i = 0; i < 9; ++i)
    {
        const float3 sampleDirection =
            direction + radius * (tangent * offsets[i].x + bitangent * offsets[i].y);
        // Compare against the local receiver plane, avoiding filter-induced slope acne.
        const float projected = dot(geometricNormal, normalize(sampleDirection));
        const float sampleDistance =
            plane * projected > 1e-6 ? distance * plane / projected : distance;
        visibility += pointShadowCompare(sampleDirection, sampleDistance - bias);
    }
    return visibility / 9.0;
}

float shadowVisibility(float3 worldPosition, float nDotL)
{
    if (scene.shadowParams.x == 0.0)
        return 1.0;
    const float4 lightPosition = mul(scene.lightViewProjection, float4(worldPosition, 1.0));
    if (lightPosition.w <= 0.0)
        return 1.0;
    const float3 position = lightPosition.xyz / lightPosition.w;
    const float2 uv = position.xy * 0.5 + 0.5;
    if (position.z < 0.0 || position.z > 1.0 || any(uv < 0.0) || any(uv > 1.0))
        return 1.0;
    uint width, height, levels;
    shadowMap.GetDimensions(0, width, height, levels);
    const float2 texel = 1.0 / float2(width, height);
    const float depth = position.z - scene.shadowParams.y * (2.0 - nDotL);
    const int radius = scene.shadowParams.z != 0.0 ? 1 : 0;
    float visibility = 0.0;
    for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x)
        {
            const float closest = shadowMap.SampleLevel(uv + float2(x, y) * texel, 0.0).r;
            visibility += depth <= closest ? 1.0 : 0.0;
        }
    return visibility / float((2 * radius + 1) * (2 * radius + 1));
}

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    const float4 worldPosition = mul(constants.model, float4(input.position, 1.0));
    output.position = mul(scene.viewProjection, worldPosition);
    output.worldPosition = worldPosition.xyz;
    output.uv = input.uv;
    // Valid for rotation and uniform scale; nonuniform scale needs inverse-transpose.
    output.worldNormal = mul((float3x3)constants.model, input.normal);
    output.worldTangent = float4(mul((float3x3)constants.model, input.tangent.xyz), input.tangent.w);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input, bool frontFacing : SV_IsFrontFace) : SV_Target0
{
    const float4 color = albedo.Sample(input.uv) * constants.baseColorFactor;
    alphaTest(color.a);
    float3 normal = normalize(input.worldNormal);
    if (constants.materialParams.x != 0.0)
    {
        float3 mappedNormal = normalMap.Sample(input.uv).xyz * 2.0 - 1.0;
        mappedNormal.xy *= constants.materialParams.x;
        const float3 tangent = normalize(input.worldTangent.xyz -
                                         normal * dot(normal, input.worldTangent.xyz));
        const float3 bitangent = cross(normal, tangent) * input.worldTangent.w;
        normal = normalize(tangent * mappedNormal.x + bitangent * mappedNormal.y +
                           normal * mappedNormal.z);
    }
    // Reverse the entire mapped normal on back faces, including tangent-space detail.
    const float faceSign = frontFacing ? 1.0 : -1.0;
    normal *= faceSign;
    const float2 mr = metallicRoughnessMap.Sample(input.uv).gb;
    const float metallic = saturate(mr.y * constants.materialParams.y);
    const float roughness = clamp(mr.x * constants.materialParams.z, 0.045, 1.0);
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), color.rgb, metallic);
    const float3 toCamera = scene.cameraPosition.xyz - input.worldPosition;
    const float distance = length(toCamera);
    const float3 viewDirection = toCamera / max(distance, 1e-6);
    const float nDotV = saturate(dot(normal, viewDirection));
    const float3 environmentFresnel = fresnelSchlick(nDotV, f0);
    const float3 irradiance = irradianceMap.Sample(environmentUV(normal)).rgb;
    float3 radiance = (1.0 - environmentFresnel) * (1.0 - metallic) * color.rgb *
                      irradiance * (scene.lighting.x / PI);
    if (nDotV > 0.0)
    {
        uint width, height, mipCount;
        prefilteredMap.GetDimensions(0, width, height, mipCount);
        const float3 reflection = reflect(-viewDirection, normal);
        const float lod = roughness * float(mipCount - 1);
        const float3 prefiltered = prefilteredMap.SampleLevel(environmentUV(reflection), lod).rgb;
        const float2 brdf = brdfLut.Sample(float2(nDotV, roughness)).rg;
        // The LUT already integrates Schlick Fresnel and Smith visibility.
        radiance += prefiltered * (f0 * brdf.x + brdf.y) * scene.lighting.z;
    }
    // Baked AO attenuates indirect lighting, before adding the direct light.
    const float ao = lerp(1.0, occlusionMap.Sample(input.uv).r, constants.materialParams.w);
    radiance *= ao;
    const float3 lightDirection = normalize(scene.lightDirection.xyz);
    const float geometricNDotL =
        saturate(dot(normalize(input.worldNormal) * faceSign, lightDirection));
    radiance += directBRDF(normal, viewDirection, lightDirection, color.rgb, metallic, f0,
                           alphaSquared) * scene.lightColor.rgb *
                shadowVisibility(input.worldPosition, geometricNDotL);
    const float3 toLight = scene.pointPositionRange.xyz - input.worldPosition;
    const float distanceSquared = dot(toLight, toLight);
    const float range = scene.pointPositionRange.w;
    if (scene.pointColorIntensity.w > 0.0 && distanceSquared > 1e-8 && range > 0.0)
    {
        const float ratioSquared = distanceSquared / (range * range);
        // Inverse-square falloff with a smooth range cutoff; soften the nearest 0.1 units.
        const float attenuation = saturate(1.0 - ratioSquared * ratioSquared) /
                                  max(distanceSquared, 0.01);
        const float3 pointDirection = toLight * rsqrt(distanceSquared);
        float angular = 1.0;
        if (scene.spotDirectionEnabled.w != 0.0)
        {
            const float cosine = dot(scene.spotDirectionEnabled.xyz, -pointDirection);
            angular = saturate((cosine - scene.spotCone.y) /
                               max(scene.spotCone.x - scene.spotCone.y, 1e-6));
            angular *= angular;
        }
        if (angular > 0.0)
        {
            const float3 pointNormal = normalize(input.worldNormal) * faceSign;
            const float pointNDotL = saturate(dot(pointNormal, pointDirection));
            const float visibility =
                pointShadowVisibility(-toLight, sqrt(distanceSquared), pointNDotL, pointNormal);
            radiance += directBRDF(normal, viewDirection, pointDirection, color.rgb,
                                   metallic, f0, alphaSquared) * scene.pointColorIntensity.rgb *
                        (scene.pointColorIntensity.w * attenuation * visibility * angular);
        }
    }
    // Emission is independent of reflected lighting, AO and shadow visibility.
    radiance += emissiveMap.Sample(input.uv).rgb * constants.emissiveFactor.rgb;
    // Keep the RGBA16F target finite for nearby bright lights.
    return float4(min(radiance, 65504.0), constants.surfaceParams.y != 0.0 ? color.a : 1.0);
}
