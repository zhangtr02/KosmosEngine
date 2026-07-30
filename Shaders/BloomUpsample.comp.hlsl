[[vk::combinedImageSampler]]
[[vk::binding(0, 0)]]
Texture2D<float4> lowerMipTexture : register(t0, space0);

[[vk::combinedImageSampler]]
[[vk::binding(0, 0)]]
SamplerState lowerMipSampler : register(s0, space0);

[[vk::binding(1, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> higherMipTexture : register(u1, space0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint higherWidth = 0;
    uint higherHeight = 0;
    higherMipTexture.GetDimensions(higherWidth, higherHeight);

    if (dispatchThreadId.x >= higherWidth || dispatchThreadId.y >= higherHeight) return;

    uint lowerWidth = 0;
    uint lowerHeight = 0;
    lowerMipTexture.GetDimensions(lowerWidth, lowerHeight);

    const float2 textureCoordinate = (float2(dispatchThreadId.xy) + 0.5) / float2(higherWidth, higherHeight);
    const float2 texelSize = 1.0 / float2(lowerWidth, lowerHeight);
    float3 upsampledColor = 0.0;
    upsampledColor += lowerMipTexture.SampleLevel(lowerMipSampler, textureCoordinate + texelSize * float2(-1.0, -1.0), 0.0).rgb;
    upsampledColor += lowerMipTexture.SampleLevel(lowerMipSampler, textureCoordinate + texelSize * float2(0.0, -1.0), 0.0).rgb * 2.0;
    upsampledColor += lowerMipTexture.SampleLevel(lowerMipSampler, textureCoordinate + texelSize * float2(1.0, -1.0), 0.0).rgb;
    upsampledColor += lowerMipTexture.SampleLevel(lowerMipSampler, textureCoordinate + texelSize * float2(-1.0, 0.0), 0.0).rgb * 2.0;
    upsampledColor += lowerMipTexture.SampleLevel(lowerMipSampler, textureCoordinate, 0.0).rgb * 4.0;
    upsampledColor += lowerMipTexture.SampleLevel(lowerMipSampler, textureCoordinate + texelSize * float2(1.0, 0.0), 0.0).rgb * 2.0;
    upsampledColor += lowerMipTexture.SampleLevel(lowerMipSampler, textureCoordinate + texelSize * float2(-1.0, 1.0), 0.0).rgb;
    upsampledColor += lowerMipTexture.SampleLevel(lowerMipSampler, textureCoordinate + texelSize * float2(0.0, 1.0), 0.0).rgb * 2.0;
    upsampledColor += lowerMipTexture.SampleLevel(lowerMipSampler, textureCoordinate + texelSize * float2(1.0, 1.0), 0.0).rgb;
    upsampledColor *= 1.0 / 16.0;

    const float3 currentColor = higherMipTexture[dispatchThreadId.xy].rgb;
    higherMipTexture[dispatchThreadId.xy] = float4(currentColor + upsampledColor, 1.0);
}