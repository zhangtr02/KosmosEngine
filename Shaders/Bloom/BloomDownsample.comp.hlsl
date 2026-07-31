[[vk::combinedImageSampler]]
[[vk::binding(0, 0)]]
Texture2D<float4> sourceTexture : register(t0, space0);

[[vk::combinedImageSampler]]
[[vk::binding(0, 0)]]
SamplerState sourceSampler : register(s0, space0);

[[vk::binding(1, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> destinationTexture : register(u1, space0);

struct BloomDownsamplePushConstant
{
    float threshold;
    float knee;
    uint applyThreshold;
};

[[vk::push_constant]]
ConstantBuffer<BloomDownsamplePushConstant> bloom;

float3 ApplySoftThreshold(float3 color)
{
    const float brightness = max(color.r, max(color.g, color.b));
    float softContribution = clamp(brightness - bloom.threshold + bloom.knee, 0.0, 2.0 * bloom.knee);
    softContribution = softContribution * softContribution / (4.0 * bloom.knee + 0.00001);
    const float contribution = max(brightness - bloom.threshold, softContribution) / max(brightness, 0.00001);
    return color * contribution;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint destinationWidth = 0;
    uint destinationHeight = 0;
    destinationTexture.GetDimensions(destinationWidth, destinationHeight);

    if (dispatchThreadId.x >= destinationWidth || dispatchThreadId.y >= destinationHeight)
    {
        return;
    }

    uint sourceWidth = 0;
    uint sourceHeight = 0;
    sourceTexture.GetDimensions(sourceWidth, sourceHeight);

    const float2 textureCoordinate = (float2(dispatchThreadId.xy) + 0.5) / float2(destinationWidth, destinationHeight);
    const float2 texelSize = 1.0 / float2(sourceWidth, sourceHeight);
    float3 color = 0.0;
    color += sourceTexture.SampleLevel(sourceSampler, textureCoordinate + texelSize * float2(-0.5, -0.5), 0.0).rgb;
    color += sourceTexture.SampleLevel(sourceSampler, textureCoordinate + texelSize * float2(0.5, -0.5), 0.0).rgb;
    color += sourceTexture.SampleLevel(sourceSampler, textureCoordinate + texelSize * float2(-0.5, 0.5), 0.0).rgb;
    color += sourceTexture.SampleLevel(sourceSampler, textureCoordinate + texelSize * float2(0.5, 0.5), 0.0).rgb;
    color *= 0.25;

    if (bloom.applyThreshold != 0)
    {
        color = ApplySoftThreshold(color);
    }
    destinationTexture[dispatchThreadId.xy] = float4(color, 1.0);
}
