#include "fullscreen.slang"

[[vk::binding(0, 0)]] Sampler2D source;
struct Parameters
{
    float4 filter; // XY = blur direction, Z = threshold, W = extract instead of blur.
};
[[vk::push_constant]] ConstantBuffer<Parameters> parameters;

float3 extractBright(float2 uv)
{
    const float3 color = source.SampleLevel(uv, 0.0).rgb;
    const float brightness = max(color.r, max(color.g, color.b));
    return color * (max(brightness - parameters.filter.z, 0.0) / max(brightness, 1e-6));
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    if (parameters.filter.w != 0.0)
    {
        uint width, height, levels;
        source.GetDimensions(0, width, height, levels);
        const float2 halfTexel = 0.5 / float2(width, height);
        // Extract before averaging so isolated bright pixels survive downsampling.
        const float3 bright = extractBright(input.uv + halfTexel * float2(-1, -1)) +
                              extractBright(input.uv + halfTexel * float2(1, -1)) +
                              extractBright(input.uv + halfTexel * float2(-1, 1)) +
                              extractBright(input.uv + halfTexel * float2(1, 1));
        return float4(bright * 0.25, 1.0);
    }
    // Normalized nine-tap Gaussian, sigma = 2 texels.
    const float weights[5] = {0.204164, 0.180174, 0.123832, 0.066282, 0.027631};
    float3 sum = source.SampleLevel(input.uv, 0.0).rgb * weights[0];
    for (int i = 1; i <= 4; ++i)
    {
        const float2 offset = parameters.filter.xy * float(i);
        sum += (source.SampleLevel(input.uv - offset, 0.0).rgb +
                source.SampleLevel(input.uv + offset, 0.0).rgb) * weights[i];
    }
    return float4(sum, 1.0);
}
