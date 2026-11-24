#pragma once

[[vk::binding(0, 1)]] StructuredBuffer<float4x4> jointMatrices;

float4x4 skinnedTransform(float4x4 model, uint offset, uint4 joints0, float4 weights0,
                          uint4 joints1, float4 weights1)
{
    if (offset == 0)
        return model;

    float4x4 pose = (float4x4)0;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        if (weights0[i] > 0.0)
            pose += weights0[i] * jointMatrices[offset + joints0[i]];
        if (weights1[i] > 0.0)
            pose += weights1[i] * jointMatrices[offset + joints1[i]];
    }
    return mul(model, pose);
}
