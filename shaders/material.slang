#pragma once

static const uint BlendFlag = 1;
static const uint UnlitFlag = 2;
static const uint FlatMorphNormalsFlag = 4;
static const uint DeriveTangentFlag = 8;
static const uint UvIndexShift = 4;

static const uint BaseColorSlot = 0;
static const uint NormalSlot = 1;
static const uint MetallicRoughnessSlot = 2;
static const uint OcclusionSlot = 3;
static const uint EmissiveSlot = 4;

struct PushConstants
{
    float4x4 model;
    float4 baseColorFactor;
    float4 materialParams; // Normal scale, metallic factor, roughness factor, AO strength.
    float3 emissiveFactor; // Linear RGB, including material strength and the UI multiplier.
    uint jointOffset;
    float alphaCutoff;
    int shadowView;
    uint flags;
    uint morphWeightOffset;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> constants;
[[vk::binding(0, 0)]] Sampler2D albedo;

struct UvTransform
{
    float4 axes;
    float4 offset;
};

struct MaterialUvTransforms
{
    UvTransform slots[5];
};

[[vk::binding(2, 1)]] StructuredBuffer<MaterialUvTransforms> uvTransforms;

float2 transformUv(float2 uv, uint slot)
{
    const uint index = constants.flags >> UvIndexShift;
    if (index == 0)
        return uv;
    const UvTransform transform = uvTransforms[index].slots[slot];
    return transform.offset.xy + transform.axes.xy * uv.x + transform.axes.zw * uv.y;
}

float4 sampleBaseColor(float2 uv, float4 vertexColor)
{
    return albedo.Sample(uv) * constants.baseColorFactor * vertexColor;
}

void alphaTest(float alpha)
{
    if (alpha < constants.alphaCutoff)
        discard;
}
