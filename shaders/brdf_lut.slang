#include "brdf.slang"

[[vk::binding(1, 0)]] [format("rgba32f")] RWTexture2D<float4> brdfLut;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    brdfLut.GetDimensions(width, height);
    if (id.x >= width || id.y >= height)
        return;
    // X = NdotV, Y = perceptual roughness; match the material's roughness floor.
    const float nDotV = (float(id.x) + 0.5) / float(width);
    const float roughness = max((float(id.y) + 0.5) / float(height), 0.045);
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float3 view = float3(sqrt(1.0 - nDotV * nDotV), 0.0, nDotV);
    const uint sampleCount = 1024;
    float2 sum = 0.0;
    for (uint i = 0; i < sampleCount; ++i)
    {
        const float3 h = sampleGGX(hammersley(i, sampleCount), alphaSquared);
        const float vDotH = saturate(dot(view, h));
        const float3 light = reflect(-view, h);
        const float nDotL = saturate(light.z);
        if (nDotL > 0.0)
        {
            const float weight =
                4.0 * visibilitySmith(nDotL, nDotV, alphaSquared) * nDotL * vDotH / max(h.z, 1e-6);
            const float fresnel = pow(1.0 - vDotH, 5.0);
            sum += float2(1.0 - fresnel, fresnel) * weight;
        }
    }
    // Integral = F0 * A + B (Schlick Fresnel split into scale and bias).
    brdfLut[id.xy] = float4(sum / float(sampleCount), 0.0, 1.0);
}
