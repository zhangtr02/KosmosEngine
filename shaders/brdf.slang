#pragma once

#include "environment.slang"

float distributionGGX(float nDotH, float alphaSquared)
{
    const float d = nDotH * nDotH * (alphaSquared - 1.0) + 1.0;
    return alphaSquared / (PI * d * d);
}

// Height-correlated Smith visibility: G / (4 * NdotL * NdotV).
float visibilitySmith(float nDotL, float nDotV, float alphaSquared)
{
    const float v = nDotL * sqrt(nDotV * nDotV * (1.0 - alphaSquared) + alphaSquared);
    const float l = nDotV * sqrt(nDotL * nDotL * (1.0 - alphaSquared) + alphaSquared);
    return 0.5 / max(v + l, 1e-6);
}

float3 fresnelSchlick(float vDotH, float3 f0)
{
    return f0 + (1.0 - f0) * pow(1.0 - vDotH, 5.0);
}

float distributionCharlie(float nDotH, float roughness)
{
    const float exponent = 1.0 / (roughness * roughness);
    return (2.0 + exponent) * pow(saturate(1.0 - nDotH * nDotH), 0.5 * exponent) /
           (2.0 * PI);
}

float visibilityNeubelt(float nDotL, float nDotV)
{
    return saturate(0.25 / max(nDotL + nDotV - nDotL * nDotV, 1e-6));
}

float3 sampleCharlie(float2 xi, float roughness)
{
    const float phi = 2.0 * PI * xi.x;
    const float alpha = roughness * roughness;
    const float sine = pow(xi.y, alpha / (2.0 * alpha + 1.0));
    return float3(sine * cos(phi), sine * sin(phi), sqrt(saturate(1.0 - sine * sine)));
}

// GGX half-vector in tangent space (normal = +Z), pdf = D * NdotH.
float3 sampleGGX(float2 xi, float alphaSquared)
{
    const float phi = 2.0 * PI * xi.x;
    const float cosine = sqrt((1.0 - xi.y) / (1.0 + (alphaSquared - 1.0) * xi.y));
    const float sine = sqrt(saturate(1.0 - cosine * cosine));
    return float3(sine * cos(phi), sine * sin(phi), cosine);
}
