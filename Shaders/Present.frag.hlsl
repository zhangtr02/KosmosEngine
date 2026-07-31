struct PresentPushConstant
{
    uint debugView;
};

[[vk::push_constant]]
ConstantBuffer<PresentPushConstant> present;

[[vk::combinedImageSampler]]
[[vk::binding(0, 0)]]
Texture2D<float4> finalColorTexture : register(t0, space0);

[[vk::combinedImageSampler]]
[[vk::binding(0, 0)]]
SamplerState finalColorSampler : register(s0, space0);

[[vk::combinedImageSampler]]
[[vk::binding(1, 0)]]
Texture2D<float4> albedoAmbientOcclusionTexture : register(t1, space0);

[[vk::combinedImageSampler]]
[[vk::binding(1, 0)]]
SamplerState albedoAmbientOcclusionSampler : register(s1, space0);

[[vk::combinedImageSampler]]
[[vk::binding(2, 0)]]
Texture2D<float4> normalRoughnessTexture : register(t2, space0);

[[vk::combinedImageSampler]]
[[vk::binding(2, 0)]]
SamplerState normalRoughnessSampler : register(s2, space0);

[[vk::combinedImageSampler]]
[[vk::binding(3, 0)]]
Texture2D<float4> materialParametersTexture : register(t3, space0);

[[vk::combinedImageSampler]]
[[vk::binding(3, 0)]]
SamplerState materialParametersSampler : register(s3, space0);

[[vk::binding(4, 0)]]
Texture2D<float> depthTexture : register(t4, space0);

[[vk::combinedImageSampler]]
[[vk::binding(5, 0)]]
Texture2D<float> rawAmbientOcclusionTexture : register(t5, space0);

[[vk::combinedImageSampler]]
[[vk::binding(5, 0)]]
SamplerState rawAmbientOcclusionSampler : register(s5, space0);

[[vk::combinedImageSampler]]
[[vk::binding(6, 0)]]
Texture2D<float> ambientOcclusionTexture : register(t6, space0);

[[vk::combinedImageSampler]]
[[vk::binding(6, 0)]]
SamplerState ambientOcclusionSampler : register(s6, space0);

[[vk::combinedImageSampler]]
[[vk::binding(7, 0)]]
Texture2D<float4> sceneColorTexture : register(t7, space0);

[[vk::combinedImageSampler]]
[[vk::binding(7, 0)]]
SamplerState sceneColorSampler : register(s7, space0);

[[vk::combinedImageSampler]]
[[vk::binding(8, 0)]]
Texture2D<float4> bloomTexture : register(t8, space0);

[[vk::combinedImageSampler]]
[[vk::binding(8, 0)]]
SamplerState bloomSampler : register(s8, space0);

struct PSInput
{
    [[vk::location(0)]] float2 textureCoordinate : TEXCOORD0;
};

float3 ToneMapDebug(float3 color)
{
    color = max(color, 0.0);
    return color / (color + 1.0);
}

float4 main(PSInput input) : SV_TARGET
{
    float3 color = finalColorTexture.Sample(finalColorSampler, input.textureCoordinate).rgb;

    if (present.debugView == 1)
    {
        color = albedoAmbientOcclusionTexture.Sample(albedoAmbientOcclusionSampler, input.textureCoordinate).rgb;
    }
    else if (present.debugView == 2)
    {
        const float3 worldNormal = normalRoughnessTexture.Sample(normalRoughnessSampler, input.textureCoordinate).xyz;
        color = worldNormal * 0.5 + 0.5;
    }
    else if (present.debugView == 3)
    {
        color = normalRoughnessTexture.Sample(normalRoughnessSampler, input.textureCoordinate).aaa;
    }
    else if (present.debugView == 4)
    {
        color = materialParametersTexture.Sample(materialParametersSampler, input.textureCoordinate).rrr;
    }
    else if (present.debugView == 5)
    {
        uint textureWidth = 0;
        uint textureHeight = 0;
        depthTexture.GetDimensions(textureWidth, textureHeight);
        const uint2 pixelCoordinate = min(uint2(input.textureCoordinate * float2(textureWidth, textureHeight)), uint2(textureWidth - 1, textureHeight - 1));
        const float depth = depthTexture.Load(int3(pixelCoordinate, 0));
        color = pow(saturate(1.0 - depth), 0.2).xxx;
    }
    else if (present.debugView == 6)
    {
        color = rawAmbientOcclusionTexture.Sample(rawAmbientOcclusionSampler, input.textureCoordinate).rrr;
    }
    else if (present.debugView == 7)
    {
        color = ambientOcclusionTexture.Sample(ambientOcclusionSampler, input.textureCoordinate).rrr;
    }
    else if (present.debugView == 8)
    {
        color = ToneMapDebug(sceneColorTexture.Sample(sceneColorSampler, input.textureCoordinate).rgb);
    }
    else if (present.debugView == 9)
    {
        color = ToneMapDebug(bloomTexture.Sample(bloomSampler, input.textureCoordinate).rgb);
    }

    return float4(color, 1.0);
}
