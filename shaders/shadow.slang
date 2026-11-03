#include "scene.slang"

struct PushConstants
{
    float4x4 model;
};
[[vk::push_constant]] ConstantBuffer<PushConstants> constants;

[shader("vertex")]
float4 vertexMain([[vk::location(0)]] float3 position : POSITION) : SV_Position
{
    return mul(scene.lightViewProjection, mul(constants.model, float4(position, 1.0)));
}
