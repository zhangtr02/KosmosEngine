[[vk::combinedImageSampler]]
[[vk::binding(0, 0)]]
Texture2D<float4> sceneColorTexture : register(t0, space0);

[[vk::combinedImageSampler]]
[[vk::binding(0, 0)]]
SamplerState sceneColorSampler : register(s0, space0);

struct PSInput
{
    [[vk::location(0)]] float2 textureCoordinate : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    return sceneColorTexture.Sample(sceneColorSampler, input.textureCoordinate);
}