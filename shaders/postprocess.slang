#include "scene.slang"
#include "fullscreen.slang"

[[vk::binding(0, 0)]] Sampler2D hdrScene;
[[vk::binding(2, 0)]] Sampler2D bloomMap;

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    const float3 radiance = hdrScene.Sample(input.uv).rgb +
                            bloomMap.Sample(input.uv).rgb * scene.lighting.w;
    const float3 exposed = radiance * scene.lighting.y;
    // Reinhard; the sRGB attachment performs the output transfer function.
    return float4(exposed / (1.0 + exposed), 1.0);
}
