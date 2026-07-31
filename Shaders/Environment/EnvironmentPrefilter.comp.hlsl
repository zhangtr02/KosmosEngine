static const float PI = 3.14159265359;

[[vk::combinedImageSampler]]
[[vk::binding(0, 0)]]
TextureCube<float4> sourceEnvironment : register(t0, space0);

[[vk::combinedImageSampler]]
[[vk::binding(0, 0)]]
SamplerState sourceSampler : register(s0, space0);

[[vk::binding(1, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2DArray<float4> prefilteredEnvironment : register(u1, space0);

struct EnvironmentPrefilterPushConstant
{
    float roughness;
    float sourceResolution;
    uint sampleCount;
    float maxSourceLod;
};

[[vk::push_constant]]
ConstantBuffer<EnvironmentPrefilterPushConstant> prefilter;

float RadicalInverseVanDerCorput(uint bits)
{
    return float(reversebits(bits)) * 2.3283064365386963e-10;
}

float2 Hammersley(uint sampleIndex, uint sampleCount)
{
    return float2(float(sampleIndex) / float(sampleCount), RadicalInverseVanDerCorput(sampleIndex));
}

float DistributionGGX(float normalDotHalf, float roughness)
{
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float denominator = normalDotHalf * normalDotHalf * (alphaSquared - 1.0) + 1.0;
    return alphaSquared / max(PI * denominator * denominator, 0.000001);
}

float3 ImportanceSampleGGX(float2 xi, float3 normal, float roughness)
{
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float phi = 2.0 * PI * xi.x;
    const float cosineTheta = sqrt((1.0 - xi.y) / (1.0 + (alphaSquared - 1.0) * xi.y));
    const float sineTheta = sqrt(max(1.0 - cosineTheta * cosineTheta, 0.0));
    const float3 localHalf = float3(cos(phi) * sineTheta, sin(phi) * sineTheta, cosineTheta);
    const float3 up = abs(normal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    const float3 tangent = normalize(cross(up, normal));
    const float3 bitangent = cross(normal, tangent);
    return normalize(tangent * localHalf.x + bitangent * localHalf.y + normal * localHalf.z);
}

float3 CubeDirection(uint face, float2 coordinate)
{
    if (face == 0) return normalize(float3(1.0, -coordinate.y, -coordinate.x));
    if (face == 1) return normalize(float3(-1.0, -coordinate.y, coordinate.x));
    if (face == 2) return normalize(float3(coordinate.x, 1.0, coordinate.y));
    if (face == 3) return normalize(float3(coordinate.x, -1.0, -coordinate.y));
    if (face == 4) return normalize(float3(coordinate.x, -coordinate.y, 1.0));
    return normalize(float3(-coordinate.x, -coordinate.y, -1.0));
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint width = 0;
    uint height = 0;
    uint layers = 0;
    prefilteredEnvironment.GetDimensions(width, height, layers);

    if (dispatchThreadId.x >= width || dispatchThreadId.y >= height || dispatchThreadId.z >= layers)
    {
        return;
    }

    const float2 coordinate = (float2(dispatchThreadId.xy) + 0.5) / float2(width, height) * 2.0 - 1.0;
    const float3 normal = CubeDirection(dispatchThreadId.z, coordinate);

    if (prefilter.roughness <= 0.0001)
    {
        prefilteredEnvironment[dispatchThreadId] = float4(sourceEnvironment.SampleLevel(sourceSampler, normal, 0.0).rgb, 1.0);
        return;
    }

    const float3 viewDirection = normal;
    const float texelSolidAngle = 4.0 * PI / (6.0 * prefilter.sourceResolution * prefilter.sourceResolution);
    float3 color = 0.0;
    float totalWeight = 0.0;

    [loop]
    for (uint sampleIndex = 0; sampleIndex < prefilter.sampleCount; ++sampleIndex)
    {
        const float3 halfDirection = ImportanceSampleGGX(Hammersley(sampleIndex, prefilter.sampleCount), normal, prefilter.roughness);
        const float3 lightDirection = normalize(2.0 * dot(viewDirection, halfDirection) * halfDirection - viewDirection);
        const float normalDotLight = saturate(dot(normal, lightDirection));

        if (normalDotLight <= 0.0)
        {
            continue;
        }

        const float normalDotHalf = saturate(dot(normal, halfDirection));
        const float viewDotHalf = saturate(dot(viewDirection, halfDirection));
        const float distribution = DistributionGGX(normalDotHalf, prefilter.roughness);
        const float probabilityDensity = max(distribution * normalDotHalf / max(4.0 * viewDotHalf, 0.000001), 0.000001);
        const float sampleSolidAngle = 1.0 / (float(prefilter.sampleCount) * probabilityDensity);
        const float sourceLod = clamp(0.5 * log2(sampleSolidAngle / texelSolidAngle), 0.0, prefilter.maxSourceLod);

        color += sourceEnvironment.SampleLevel(sourceSampler, lightDirection, sourceLod).rgb * normalDotLight;
        totalWeight += normalDotLight;
    }

    prefilteredEnvironment[dispatchThreadId] = float4(color / max(totalWeight, 0.000001), 1.0);
}