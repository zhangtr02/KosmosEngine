#include "scene.slang"

[[vk::binding(0, 0)]] Sampler2D hdrScene;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(uint id : SV_VulkanVertexID)
{
    VertexOutput output;
    output.uv = float2((id << 1) & 2, id & 2);
    output.position = float4(output.uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    const float3 exposed = hdrScene.Sample(input.uv).rgb * scene.lighting.y;
    // Reinhard; the sRGB attachment performs the output transfer function.
    return float4(exposed / (1.0 + exposed), 1.0);
}
