#pragma once

struct PushConstants
{
    float4x4 model;
    float4 baseColorFactor;
    float4 materialParams; // Normal scale, metallic factor, roughness factor, AO strength.
    float4 emissiveFactor; // Linear RGB, including material strength and the UI multiplier.
    float4 surfaceParams; // Alpha cutoff, BLEND, shadow view (-2 Spot, -1 Sun, 0..5 Cube), unused.
};

[[vk::push_constant]] ConstantBuffer<PushConstants> constants;
[[vk::binding(0, 0)]] Sampler2D albedo;

void alphaTest(float alpha)
{
    if (alpha < constants.surfaceParams.x)
        discard;
}
