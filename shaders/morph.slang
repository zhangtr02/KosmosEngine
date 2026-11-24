#pragma once

struct MorphDelta
{
    float4 position;
    float4 normal;
    float4 tangent;
};

[[vk::binding(13, 0)]] StructuredBuffer<MorphDelta> morphDeltas;
[[vk::binding(1, 1)]] StructuredBuffer<float> morphWeights;

MorphDelta morphVertex(uint2 range, uint weightOffset)
{
    MorphDelta result = {0.0, 0.0, 0.0};
    for (uint target = 0; target < range.y; ++target)
    {
        const float weight = morphWeights[weightOffset + target];
        if (weight == 0.0)
            continue;
        const MorphDelta delta = morphDeltas[range.x + target];
        result.position += weight * delta.position;
        result.normal += weight * delta.normal;
        result.tangent += weight * delta.tangent;
    }
    return result;
}
