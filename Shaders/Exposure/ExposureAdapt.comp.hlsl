struct ExposureAdaptPushConstant
{
    float deltaTime;
    float increaseSpeed;
    float decreaseSpeed;
    float minimumExposure;
    float maximumExposure;
};

[[vk::push_constant]]
ConstantBuffer<ExposureAdaptPushConstant> adaptation;

[[vk::binding(0, 0)]]
Texture2D<float2> luminanceStatisticsTexture : register(t0, space0);

[[vk::binding(1, 0)]]
Texture2D<float> previousExposureTexture : register(t1, space0);

[[vk::binding(2, 0)]]
[[vk::image_format("r32f")]]
RWTexture2D<float> currentExposureTexture : register(u2, space0);

[numthreads(1, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const float2 statistics = luminanceStatisticsTexture.Load(int3(0, 0, 0));
    const float averageLogLuminance = statistics.x / max(statistics.y, 1.0);
    const float averageLuminance = exp(averageLogLuminance);
    const float targetExposure = clamp(0.18 / max(averageLuminance, 0.0001), adaptation.minimumExposure, adaptation.maximumExposure);
    const float previousExposure = max(previousExposureTexture.Load(int3(0, 0, 0)), 0.0001);
    const float speed = targetExposure > previousExposure ? adaptation.increaseSpeed : adaptation.decreaseSpeed;
    const float factor = 1.0 - exp(-max(speed, 0.0) * adaptation.deltaTime);
    currentExposureTexture[uint2(0, 0)] = lerp(previousExposure, targetExposure, saturate(factor));
}