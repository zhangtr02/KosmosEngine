#include "scene.slang"
#include "environment.slang"

[[vk::binding(4, 0)]] Sampler2D environmentMap;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 ndc : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(uint id : SV_VulkanVertexID)
{
    VertexOutput output;
    const float2 p = float2((id << 1) & 2, id & 2) * 2.0 - 1.0;
    output.position = float4(p, 0.0, 1.0);
    output.ndc = p;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    const float4 world = mul(scene.inverseSkyViewProjection, float4(input.ndc, 1.0, 1.0));
    const float3 direction = normalize(world.xyz);
    return float4(environmentMap.SampleLevel(environmentUV(direction), 0.0).rgb, 1.0);
}
