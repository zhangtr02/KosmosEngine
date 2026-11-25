#pragma once

[[vk::binding(0, 1)]] StructuredBuffer<float4x4> jointMatrices;

float4x4 skinnedTransform(float4x4 model, uint offset, uint4 joints, float4 weights)
{
    if (offset == 0)
        return model;

    float4x4 pose = (float4x4)0;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        if (weights[i] > 0.0)
            pose += weights[i] * jointMatrices[offset + joints[i]];
    }
    return mul(model, pose);
}
