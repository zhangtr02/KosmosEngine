#include "environment.slang"

[[vk::binding(0, 0)]] Sampler2D environmentMap;
[[vk::binding(1, 0)]] [format("rgba32f")] RWTexture2D<float4> irradianceMap;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    irradianceMap.GetDimensions(width, height);
    if (id.x >= width || id.y >= height)
        return;
    uint sourceWidth, sourceHeight, mipCount;
    environmentMap.GetDimensions(0, sourceWidth, sourceHeight, mipCount);
    const float3 normal = environmentDirection((float2(id.xy) + 0.5) / float2(width, height));
    const float3 up = abs(normal.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    const float3 tangent = normalize(cross(up, normal));
    const float3 bitangent = cross(normal, tangent);
    const uint sampleCount = 4096;
    float3 sum = 0.0;
    for (uint i = 0; i < sampleCount; ++i)
    {
        // Hammersley points, mapped to a cosine-weighted hemisphere (pdf = cos(theta) / PI).
        const float2 xi = hammersley(i, sampleCount);
        const float radius = sqrt(xi.x);
        const float phi = 2.0 * PI * xi.y;
        const float cosine = sqrt(1.0 - xi.x);
        const float3 direction = tangent * (radius * cos(phi)) +
                                 bitangent * (radius * sin(phi)) + normal * cosine;
        const float sinTheta = max(sqrt(saturate(1.0 - direction.y * direction.y)), 0.001);
        const float texelArea = 2.0 * PI * PI * sinTheta / (float(sourceWidth) * float(sourceHeight));
        const float sampleArea = PI / (float(sampleCount) * cosine);
        const float lod = clamp(0.5 * log2(sampleArea / texelArea), 0.0, float(mipCount - 1));
        sum += environmentMap.SampleLevel(environmentUV(direction), lod).rgb;
    }
    // Li * cos(theta) / pdf = PI * Li; store irradiance E, not E / PI.
    irradianceMap[id.xy] = float4(PI * sum / float(sampleCount), 1.0);
}
