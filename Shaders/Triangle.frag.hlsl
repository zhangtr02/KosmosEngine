[[vk::combinedImageSampler]]
[[vk::binding(1, 0)]]
Texture2D<float4> baseColorTexture : register(t1, space0);

[[vk::combinedImageSampler]]
[[vk::binding(1, 0)]]
SamplerState baseColorSampler : register(s1, space0);

struct PSInput
{
    [[vk::location(0)]] float3 color : COLOR0;
    [[vk::location(1)]] float2 textureCoordinate : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    const float4 textureColor = baseColorTexture.Sample(
        baseColorSampler,
        input.textureCoordinate);

    return textureColor * float4(input.color, 1.0);
}