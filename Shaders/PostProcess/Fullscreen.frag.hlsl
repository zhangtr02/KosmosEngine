struct PostProcessPushConstant
{
    float exposureCompensation;
    float bloomIntensity;
};

[[vk::push_constant]]
ConstantBuffer<PostProcessPushConstant> postProcess;

[[vk::combinedImageSampler]]
[[vk::binding(0, 0)]]
Texture2D<float4> sceneColorTexture : register(t0, space0);

[[vk::combinedImageSampler]]
[[vk::binding(0, 0)]]
SamplerState sceneColorSampler : register(s0, space0);

[[vk::combinedImageSampler]]
[[vk::binding(1, 0)]]
Texture2D<float4> bloomTexture : register(t1, space0);

[[vk::combinedImageSampler]]
[[vk::binding(1, 0)]]
SamplerState bloomSampler : register(s1, space0);

[[vk::binding(2, 0)]]
Texture2D<float> exposureTexture : register(t2, space0);

struct PSInput
{
    [[vk::location(0)]] float2 textureCoordinate : TEXCOORD0;
};

static const float3x3 ACESInputMatrix = float3x3(
    0.59719, 0.35458, 0.04823,
    0.07600, 0.90834, 0.01566,
    0.02840, 0.13383, 0.83777);

static const float3x3 ACESOutputMatrix = float3x3(
     1.60475, -0.53108, -0.07367,
    -0.10208,  1.10813, -0.00605,
    -0.00327, -0.07276,  1.07602);

float3 RRTAndODTFit(float3 color)
{
    const float3 numerator = color * (color + 0.0245786) - 0.000090537;
    const float3 denominator = color * (0.983729 * color + 0.4329510) + 0.238081;
    return numerator / denominator;
}

float3 ToneMapACES(float3 color)
{
    color = mul(ACESInputMatrix, color);
    color = RRTAndODTFit(color);
    color = mul(ACESOutputMatrix, color);
    return saturate(color);
}

float4 main(PSInput input) : SV_TARGET
{
    const float4 sceneColor = sceneColorTexture.Sample(sceneColorSampler, input.textureCoordinate);
    const float3 bloomColor = bloomTexture.Sample(bloomSampler, input.textureCoordinate).rgb;
    const float exposure = max(exposureTexture.Load(int3(0, 0, 0)), 0.0001) * max(postProcess.exposureCompensation, 0.0);
    const float3 hdrColor = max(sceneColor.rgb, 0.0) + max(bloomColor, 0.0) * postProcess.bloomIntensity;
    return float4(ToneMapACES(hdrColor * exposure), sceneColor.a);
}