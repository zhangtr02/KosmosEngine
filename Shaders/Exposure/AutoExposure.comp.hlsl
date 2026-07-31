struct AutoExposurePushConstant
{
    uint firstPass;
};

[[vk::push_constant]]
ConstantBuffer<AutoExposurePushConstant> autoExposure;

[[vk::binding(0, 0)]]
Texture2D<float4> sourceTexture : register(t0, space0);

[[vk::binding(1, 0)]]
[[vk::image_format("rg32f")]]
RWTexture2D<float2> destinationTexture : register(u1, space0);

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint destinationWidth;
    uint destinationHeight;
    destinationTexture.GetDimensions(destinationWidth, destinationHeight);

    if (dispatchThreadId.x >= destinationWidth || dispatchThreadId.y >= destinationHeight)
    {
        return;
    }

    if (autoExposure.firstPass != 0)
    {
        const float3 color = max(sourceTexture.Load(int3(dispatchThreadId.xy, 0)).rgb, 0.0);
        const float luminance = max(dot(color, float3(0.2126, 0.7152, 0.0722)), 0.0001);
        destinationTexture[dispatchThreadId.xy] = float2(log(luminance), 1.0);
        return;
    }

    uint sourceWidth;
    uint sourceHeight;
    sourceTexture.GetDimensions(sourceWidth, sourceHeight);
    const uint2 sourceSize = uint2(sourceWidth, sourceHeight);
    const uint2 destinationSize = uint2(destinationWidth, destinationHeight);
    const uint2 sourceBegin = dispatchThreadId.xy * sourceSize / destinationSize;
    const uint2 sourceEnd = (dispatchThreadId.xy + 1) * sourceSize / destinationSize;

    float2 statistics = 0.0;

    for (uint y = sourceBegin.y; y < sourceEnd.y; ++y)
    {
        for (uint x = sourceBegin.x; x < sourceEnd.x; ++x)
        {
            statistics += sourceTexture.Load(int3(uint2(x, y), 0)).rg;
        }
    }

    destinationTexture[dispatchThreadId.xy] = statistics;
}
