#include "brdf.slang"

[[vk::binding(0, 0)]] Sampler2D environmentMap;
[[vk::binding(1, 0)]] [format("rgba32f")] RWTexture2D<float4> prefilteredMap;
struct Parameters
{
    float roughness;
};
[[vk::push_constant]] ConstantBuffer<Parameters> parameters;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    prefilteredMap.GetDimensions(width, height);
    if (id.x >= width || id.y >= height)
        return;
    uint sourceWidth, sourceHeight, mipCount;
    environmentMap.GetDimensions(0, sourceWidth, sourceHeight, mipCount);
    const float2 uv = (float2(id.xy) + 0.5) / float2(width, height);
    if (parameters.roughness == 0.0)
    {
        const float lod = max(log2(float(sourceWidth) / float(width)), 0.0);
        prefilteredMap[id.xy] = float4(environmentMap.SampleLevel(uv, lod).rgb, 1.0);
        return;
    }
    // Split-sum prefilter assumes N = V = reflection direction.
    const float3 normal = environmentDirection(uv);
    const float3 up = abs(normal.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    const float3 tangent = normalize(cross(up, normal));
    const float3 bitangent = cross(normal, tangent);
    const float alpha = parameters.roughness * parameters.roughness;
    const float alphaSquared = alpha * alpha;
    const uint sampleCount = 1024;
    float3 sum = 0.0;
    float weight = 0.0;
    for (uint i = 0; i < sampleCount; ++i)
    {
        const float3 h = sampleGGX(hammersley(i, sampleCount), alphaSquared);
        const float3 halfway = tangent * h.x + bitangent * h.y + normal * h.z;
        const float3 direction = reflect(-normal, halfway);
        const float nDotL = saturate(dot(normal, direction));
        if (nDotL > 0.0)
        {
            // pdf(L) = D * NdotH / (4 * VdotH) = D / 4 when N = V.
            const float pdf = max(distributionGGX(h.z, alphaSquared) * 0.25, 1e-6);
            const float sinTheta = max(sqrt(saturate(1.0 - direction.y * direction.y)), 0.001);
            const float texelArea =
                2.0 * PI * PI * sinTheta / (float(sourceWidth) * float(sourceHeight));
            const float sampleArea = 1.0 / (float(sampleCount) * pdf);
            const float lod = clamp(0.5 * log2(sampleArea / texelArea), 0.0, float(mipCount - 1));
            sum += environmentMap.SampleLevel(environmentUV(direction), lod).rgb * nDotL;
            weight += nDotL;
        }
    }
    prefilteredMap[id.xy] = float4(sum / max(weight, 1e-6), 1.0);
}
