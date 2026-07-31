[[vk::combinedImageSampler]]
[[vk::binding(0, 0)]]
Texture2D<float4> renderViewTexture : register(t0, space0);

[[vk::combinedImageSampler]]
[[vk::binding(0, 0)]]
SamplerState renderViewSampler : register(s0, space0);

struct PSInput
{
    [[vk::location(0)]] float2 textureCoordinate : TEXCOORD0;
};

float4 main(PSInput input) : SV_TARGET
{
    return renderViewTexture.Sample(renderViewSampler, input.textureCoordinate);
}
