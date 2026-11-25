#include "scene.slang"
#include "material.slang"
#include "skinning.slang"
#include "morph.slang"

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float4 color : COLOR0;
    [[vk::location(7)]] uint4 joints : BLENDINDICES0;
    [[vk::location(8)]] float4 weights : BLENDWEIGHT0;
    [[vk::location(9)]] uint2 morph : TEXCOORD3;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float3 worldPosition : TEXCOORD1;
    float4 color : COLOR0;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input, uint instance : SV_VulkanInstanceID,
                        out float pointSize : SV_PointSize)
{
    pointSize = 1.0;
    VertexOutput output;
    const float3 delta = morphVertex(input.morph, constants.morphWeightOffset).position.xyz;
    const float4x4 model = instancedTransform(constants.model, instance);
    const float4x4 transform = skinnedTransform(model, constants.jointOffset, input.joints, input.weights);
    const float4 world = mul(transform, float4(input.position + delta, 1.0));
    output.position = constants.shadowView >= 0 ?
        mul(scene.pointViewProjections[uint(constants.shadowView)], world) :
        constants.shadowView == -2 ? mul(scene.spotViewProjection, world) :
        mul(scene.lightViewProjection, world);
    output.uv = transformUv(input.uv, BaseColorSlot);
    output.color = input.color;
    output.worldPosition = world.xyz;
    return output;
}

[shader("fragment")]
float fragmentMain(VertexOutput input) : SV_Depth
{
    if (constants.alphaCutoff >= 0.0)
        alphaTest(sampleBaseColor(input.uv, input.color).a);
    if (constants.shadowView == -1)
        return input.position.z;
    const LightData light = scene.lights[uint(scene.localShadowParams.x)];
    return length(input.worldPosition - light.positionRange.xyz) / light.positionRange.w;
}
