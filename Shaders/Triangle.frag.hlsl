struct MaterialUniform
{
    float4 baseColor;
};

[[vk::binding(0, 1)]]
ConstantBuffer<MaterialUniform> material : register(b0, space1);

[[vk::combinedImageSampler]]
[[vk::binding(1, 1)]]
Texture2D<float4> baseColorTexture : register(t1, space1);

[[vk::combinedImageSampler]]
[[vk::binding(1, 1)]]
SamplerState baseColorSampler : register(s1, space1);

struct PSInput
{
    [[vk::location(0)]] float3 color : COLOR0;
    [[vk::location(1)]] float2 textureCoordinate : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    const float4 textureColor = baseColorTexture.Sample(baseColorSampler, input.textureCoordinate);
    return textureColor * material.baseColor * float4(input.color, 1.0);
}