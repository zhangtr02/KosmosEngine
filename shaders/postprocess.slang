#include "scene.slang"
#include "fullscreen.slang"

[[vk::binding(0, 0)]] Sampler2D hdrScene;
[[vk::binding(2, 0)]] Sampler2D bloomMap;
[[vk::binding(3, 0)]] Sampler2D selectionMask;

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    const float3 radiance = hdrScene.Sample(input.uv).rgb +
                            bloomMap.Sample(input.uv).rgb * scene.lighting.w;
    const float3 exposed = radiance * scene.lighting.y;
    // Reinhard; the sRGB attachment performs the output transfer function.
    float3 color = exposed / (1.0 + exposed);
    const float2 pixel = 1.5 * float2(ddx(input.uv.x), ddy(input.uv.y));
    const float coverage = selectionMask.SampleLevel(input.uv, 0.0).r;
    if (coverage > 0.0)
    {
        const float2 x = float2(pixel.x, 0.0);
        const float2 y = float2(0.0, pixel.y);
        const float neighbors = min(
            min(selectionMask.SampleLevel(input.uv - x, 0.0).r,
                selectionMask.SampleLevel(input.uv + x, 0.0).r),
            min(selectionMask.SampleLevel(input.uv - y, 0.0).r,
                selectionMask.SampleLevel(input.uv + y, 0.0).r));
        color = lerp(color, float3(1.0, 0.38, 0.045), saturate(coverage - neighbors));
    }
    return float4(color, 1.0);
}
