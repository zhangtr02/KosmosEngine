#pragma once

#include "geometry.slang"

struct MorphDelta
{
    float4 position;
    float4 normal;
    float4 tangent;
};

[[vk::binding(1, 1)]] StructuredBuffer<float> morphWeights;

MorphDelta morphVertex(uint2 range, uint weightOffset)
{
    MorphDelta result = {0.0, 0.0, 0.0};
    for (uint target = 0; target < range.y; ++target)
    {
        const float weight = morphWeights[weightOffset + target];
        if (weight == 0.0)
            continue;
        const uint first = (range.x + target) * 3;
        result.position += weight * geometryData[first];
        result.normal += weight * geometryData[first + 1];
        result.tangent += weight * geometryData[first + 2];
    }
    return result;
}
