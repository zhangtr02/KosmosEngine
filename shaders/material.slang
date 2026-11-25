#pragma once

static const uint BlendFlag = 1;
static const uint UnlitFlag = 2;
static const uint FlatMorphNormalsFlag = 4;
static const uint DeriveTangentFlag = 8;
static const uint AnimatedUvFlag = 16;
static const uint MaterialIndexShift = 5;

static const uint BaseColorSlot = 0;
static const uint NormalSlot = 1;
static const uint MetallicRoughnessSlot = 2;
static const uint OcclusionSlot = 3;
static const uint EmissiveSlot = 4;
static const uint BaseTextureCount = 5;
static const uint MaterialTextureCount = 15;
static const uint AdvancedUvPairs = 5;
static const uint SpecularSlot = 5;
static const uint SpecularColorSlot = 6;
static const uint ClearcoatSlot = 7;
static const uint ClearcoatRoughnessSlot = 8;
static const uint ClearcoatNormalSlot = 9;
static const uint SheenColorSlot = 10;
static const uint SheenRoughnessSlot = 11;
static const uint AnisotropySlot = 12;
static const uint IridescenceSlot = 13;
static const uint IridescenceThicknessSlot = 14;

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

struct AdvancedMaterial
{
    float4 specular;
    float4 clearcoat;
    float4 sheen;
    float4 anisotropy;
    float4 iridescence;
    float4 workflow;
};

struct GpuMaterial
{
    UvTransform uv[MaterialTextureCount];
    AdvancedMaterial advanced;
};

[[vk::binding(2, 1)]] StructuredBuffer<GpuMaterial> materials;

AdvancedMaterial readAdvancedMaterial()
{
    const uint index = constants.flags >> MaterialIndexShift;
    if (index != 0)
        return materials[index].advanced;
    return {1.0, 0.0, 0.0, float4(0, 1, 0, 0), float4(0, 1.3, 100, 400), float4(1.5, 1, 0, 0)};
}

float2 transformUv(float2 uv, uint slot)
{
    if ((constants.flags & AnimatedUvFlag) == 0)
        return uv;
    const UvTransform transform = materials[constants.flags >> MaterialIndexShift].uv[slot];
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
