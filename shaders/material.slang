#pragma once

struct PushConstants
{
    float4x4 model;
    float4 baseColorFactor;
    float4 materialParams; // Normal scale, metallic factor, roughness factor, AO strength.
    float3 emissiveFactor; // Linear RGB, including material strength and the UI multiplier.
    uint jointOffset;
    float4 surfaceParams; // Alpha cutoff, BLEND, shadow view (-2 Spot, -1 Sun, 0..5 Cube), Unlit.
};

[[vk::push_constant]] ConstantBuffer<PushConstants> constants;
[[vk::binding(0, 0)]] Sampler2D albedo;

float4 sampleBaseColor(float2 uv, float4 vertexColor)
{
    return albedo.Sample(uv) * constants.baseColorFactor * vertexColor;
}

void alphaTest(float alpha)
{
    if (alpha < constants.surfaceParams.x)
        discard;
}
