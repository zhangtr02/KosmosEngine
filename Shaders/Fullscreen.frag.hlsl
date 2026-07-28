struct PostProcessPushConstant
{
    float exposure;
    float3 padding;
};

[[vk::push_constant]]
ConstantBuffer<PostProcessPushConstant> postProcess;

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

float3 ToneMapACES(float3 color)
{
    const float3 numerator = color * (2.51 * color + 0.03);
    const float3 denominator = color * (2.43 * color + 0.59) + 0.14;
    return saturate(numerator / denominator);
}

float4 main(PSInput input) : SV_TARGET
{
    const float4 sceneColor = sceneColorTexture.Sample(sceneColorSampler, input.textureCoordinate);
    const float3 positiveColor = max(sceneColor.rgb, float3(0.0, 0.0, 0.0));
    const float3 exposedColor = positiveColor * postProcess.exposure;
    const float3 mappedColor = ToneMapACES(exposedColor);

    return float4(mappedColor, sceneColor.a);
}