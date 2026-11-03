#include "scene.slang"
#include "material.slang"

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float3 worldPosition : TEXCOORD1;
};

[shader("vertex")]
VertexOutput vertexMain([[vk::location(0)]] float3 position : POSITION,
                        [[vk::location(1)]] float2 uv : TEXCOORD0)
{
    VertexOutput output;
    const float4 world = mul(constants.model, float4(position, 1.0));
    output.position = constants.surfaceParams.z >= 0.0 ?
        mul(scene.pointViewProjections[uint(constants.surfaceParams.z)], world) :
        constants.surfaceParams.z == -2.0 ? mul(scene.spotViewProjection, world) :
        mul(scene.lightViewProjection, world);
    output.uv = uv;
    output.worldPosition = world.xyz;
    return output;
}

[shader("fragment")]
float fragmentMain(VertexOutput input) : SV_Depth
{
    if (constants.surfaceParams.x >= 0.0)
        alphaTest(albedo.Sample(input.uv).a * constants.baseColorFactor.a);
    return constants.surfaceParams.z != -1.0 ?
        length(input.worldPosition - scene.pointPositionRange.xyz) / scene.pointPositionRange.w :
        input.position.z;
}
