#include "Common/PBRLighting.hlsli"

[[vk::binding(0, 1)]]
Texture2D<float4> albedoAmbientOcclusionTexture : register(t0, space1);

[[vk::binding(1, 1)]]
Texture2D<float4> normalRoughnessTexture : register(t1, space1);

[[vk::binding(2, 1)]]
Texture2D<float4> materialParametersTexture : register(t2, space1);

[[vk::binding(3, 1)]]
Texture2D<float> depthTexture : register(t3, space1);

[[vk::binding(4, 1)]]
Texture2D<float> screenSpaceAmbientOcclusionTexture : register(t4, space1);

struct PSInput
{
    float4 position : SV_POSITION;
    [[vk::location(0)]] float2 textureCoordinate : TEXCOORD0;
};

float3 ReconstructWorldPosition(float2 textureCoordinate, float depth)
{
    const float2 ndcPosition = textureCoordinate * 2.0 - 1.0;
    const float4 worldPosition = mul(camera.inverseViewProjection, float4(ndcPosition, depth, 1.0));
    return worldPosition.xyz / worldPosition.w;
}

float4 main(PSInput input) : SV_TARGET
{
    const uint2 pixelCoordinate = uint2(input.position.xy);
    const float depth = depthTexture.Load(int3(pixelCoordinate, 0));

    if (depth >= 1.0)
    {
        discard;
    }

    const float4 albedoAmbientOcclusion = albedoAmbientOcclusionTexture.Load(int3(pixelCoordinate, 0));
    const float4 normalRoughness = normalRoughnessTexture.Load(int3(pixelCoordinate, 0));
    const float4 materialParameters = materialParametersTexture.Load(int3(pixelCoordinate, 0));
    const float screenSpaceAmbientOcclusion = screenSpaceAmbientOcclusionTexture.Load(int3(pixelCoordinate, 0));
    const float3 albedo = albedoAmbientOcclusion.rgb;
    const float ambientOcclusion = saturate(albedoAmbientOcclusion.a * screenSpaceAmbientOcclusion);
    const float3 normal = normalize(normalRoughness.xyz);
    const float roughness = clamp(normalRoughness.a, 0.045, 1.0);
    const float metallic = saturate(materialParameters.r);
    const float emissiveStrength = max(materialParameters.g, 0.0);
    const float opacity = saturate(materialParameters.b);
    const float3 worldPosition = ReconstructWorldPosition(input.textureCoordinate, depth);
    const float3 lighting = EvaluatePBRLighting(albedo, metallic, roughness, ambientOcclusion, emissiveStrength, worldPosition, normal, normal);
    return float4(lighting, opacity);
}
