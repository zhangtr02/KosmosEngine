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

float pointShadowVisibility(float3 fromLight, float distance, float nDotL)
{
    if (scene.pointShadowParams.x == 0.0)
        return 1.0;
    const float closest = pointShadowMap.SampleLevel(fromLight, 0.0).r * scene.pointPositionRange.w;
    const float bias = scene.pointShadowParams.y * (2.0 - nDotL);
    return distance - bias <= closest ? 1.0 : 0.0;
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
        const float pointNDotL =
            saturate(dot(normalize(input.worldNormal) * faceSign, pointDirection));
        const float visibility = pointShadowVisibility(-toLight, sqrt(distanceSquared), pointNDotL);
        radiance += directBRDF(normal, viewDirection, pointDirection, color.rgb,
                               metallic, f0, alphaSquared) * scene.pointColorIntensity.rgb *
                    (scene.pointColorIntensity.w * attenuation * visibility);
    }
    // Emission is independent of reflected lighting, AO and shadow visibility.
    radiance += emissiveMap.Sample(input.uv).rgb * constants.emissiveFactor.rgb;
    // Keep the RGBA16F target finite for nearby bright lights.
    return float4(min(radiance, 65504.0), constants.surfaceParams.y != 0.0 ? color.a : 1.0);
}
