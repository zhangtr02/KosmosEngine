[[vk::combinedImageSampler]]
[[vk::binding(0, 1)]]
TextureCube<float4> environmentTexture : register(t0, space1);

[[vk::combinedImageSampler]]
[[vk::binding(0, 1)]]
SamplerState environmentSampler : register(s0, space1);

struct PSInput
{
    [[vk::location(0)]] float3 direction : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    return float4(environmentTexture.Sample(environmentSampler, normalize(input.direction)).rgb, 1.0);
}