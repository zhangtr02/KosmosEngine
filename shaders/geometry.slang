#pragma once

[[vk::binding(13, 0)]] StructuredBuffer<float4> geometryData;

float4x4 instancedTransform(float4x4 model, uint instance)
{
    if (instance == 0)
        return model;
    const uint first = (instance - 1) * 4;
    const float4x4 columns = float4x4(geometryData[first], geometryData[first + 1],
                                    geometryData[first + 2], geometryData[first + 3]);
    return mul(model, transpose(columns));
}
