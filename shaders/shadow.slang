#include "scene.slang"
#include "material.slang"
#include "skinning.slang"

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float4 color : COLOR0;
    [[vk::location(7)]] uint4 joints0 : BLENDINDICES0;
    [[vk::location(8)]] float4 weights0 : BLENDWEIGHT0;
    [[vk::location(9)]] uint4 joints1 : BLENDINDICES1;
    [[vk::location(10)]] float4 weights1 : BLENDWEIGHT1;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float3 worldPosition : TEXCOORD1;
    float4 color : COLOR0;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    const float4x4 transform = skinnedTransform(constants.model, constants.jointOffset,
                                                input.joints0, input.weights0,
                                                input.joints1, input.weights1);
    const float4 world = mul(transform, float4(input.position, 1.0));
    output.position = constants.surfaceParams.z >= 0.0 ?
        mul(scene.pointViewProjections[uint(constants.surfaceParams.z)], world) :
        constants.surfaceParams.z == -2.0 ? mul(scene.spotViewProjection, world) :
        mul(scene.lightViewProjection, world);
    output.uv = input.uv;
    output.color = input.color;
    output.worldPosition = world.xyz;
    return output;
}

[shader("fragment")]
float fragmentMain(VertexOutput input) : SV_Depth
{
    if (constants.surfaceParams.x >= 0.0)
        alphaTest(sampleBaseColor(input.uv, input.color).a);
    if (constants.surfaceParams.z == -1.0)
        return input.position.z;
    const LightData light = scene.lights[uint(scene.localShadowParams.x)];
    return length(input.worldPosition - light.positionRange.xyz) / light.positionRange.w;
}
