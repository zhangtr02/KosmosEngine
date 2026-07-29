[[vk::combinedImageSampler]]
[[vk::binding(3, 0)]]
TextureCube<float4> environmentTexture : register(t3, space0);

[[vk::combinedImageSampler]]
[[vk::binding(3, 0)]]
SamplerState environmentSampler : register(s3, space0);

struct PSInput
{
    [[vk::location(0)]] float3 direction : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    return float4(environmentTexture.SampleLevel(environmentSampler, normalize(input.direction), 0.0).rgb, 1.0);
}