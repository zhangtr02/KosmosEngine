static const float PI = 3.14159265359;
static const uint SampleCount = 16;

static const float3 SampleKernel[16] =
{
    float3(0.5381, 0.1856, 0.4319),
    float3(0.1379, 0.2486, 0.4430),
    float3(0.3371, 0.5679, 0.0057),
    float3(-0.6999, -0.0451, 0.0019),
    float3(0.0689, -0.1598, 0.8547),
    float3(0.0560, 0.0069, 0.1843),
    float3(-0.0146, 0.1402, 0.0762),
    float3(0.0100, -0.1924, 0.0344),
    float3(-0.3577, -0.5301, 0.4358),
    float3(-0.3169, 0.1063, 0.0158),
    float3(0.0103, -0.5869, 0.0046),
    float3(-0.0897, -0.4940, 0.3287),
    float3(0.7119, -0.0154, 0.0918),
    float3(-0.0533, 0.0596, 0.5411),
    float3(0.0352, -0.0631, 0.5460),
    float3(-0.4776, 0.2847, 0.0271)
};

struct CameraUniform
{
    float4x4 view;
    float4x4 projection;
    float4x4 inverseViewProjection;
    float4 position;
};

struct SSAOPushConstant
{
    float radius;
    float bias;
    float power;
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraUniform> camera : register(b0, space0);

[[vk::binding(0, 1)]]
Texture2D<float4> normalRoughnessTexture : register(t0, space1);

[[vk::binding(1, 1)]]
Texture2D<float> depthTexture : register(t1, space1);

[[vk::push_constant]]
ConstantBuffer<SSAOPushConstant> settings;

struct PSInput
{
    float4 position : SV_POSITION;
    [[vk::location(0)]] float2 textureCoordinate : TEXCOORD0;
};

float Hash(float2 value)
{
    return frac(sin(dot(value, float2(12.9898, 78.233))) * 43758.5453);
}

float3 ReconstructWorldPosition(float2 textureCoordinate, float depth)
{
    const float2 ndcPosition = textureCoordinate * 2.0 - 1.0;
    const float4 worldPosition = mul(camera.inverseViewProjection, float4(ndcPosition, depth, 1.0));
    return worldPosition.xyz / worldPosition.w;
}

float main(PSInput input) : SV_TARGET
{
    const uint2 pixelCoordinate = uint2(input.position.xy);
    const float centerDepth = depthTexture.Load(int3(pixelCoordinate, 0));

    if (centerDepth >= 1.0)
    {
        return 1.0;
    }

    uint textureWidth = 0;
    uint textureHeight = 0;
    depthTexture.GetDimensions(textureWidth, textureHeight);

    const uint2 textureExtent = uint2(textureWidth, textureHeight);
    const float3 centerWorldPosition = ReconstructWorldPosition(input.textureCoordinate, centerDepth);
    const float3 centerViewPosition = mul(camera.view, float4(centerWorldPosition, 1.0)).xyz;
    const float3 worldNormal = normalize(normalRoughnessTexture.Load(int3(pixelCoordinate, 0)).xyz);
    const float3 viewNormal = normalize(mul((float3x3)camera.view, worldNormal));

    const float randomAngle = Hash(float2(pixelCoordinate)) * 2.0 * PI;
    const float3 randomVector = float3(cos(randomAngle), sin(randomAngle), 0.0);
    float3 tangent = randomVector - viewNormal * dot(randomVector, viewNormal);

    if (dot(tangent, tangent) < 0.0001)
    {
        const float3 fallbackAxis = abs(viewNormal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0);
        tangent = cross(fallbackAxis, viewNormal);
    }

    tangent = normalize(tangent);
    const float3 bitangent = normalize(cross(viewNormal, tangent));
    float occlusion = 0.0;

    [unroll]
    for (uint sampleIndex = 0; sampleIndex < SampleCount; ++sampleIndex)
    {
        const float normalizedIndex = float(sampleIndex + 1) / float(SampleCount);
        const float sampleScale = lerp(0.1, 1.0, normalizedIndex * normalizedIndex);
        const float3 kernelDirection = normalize(SampleKernel[sampleIndex]) * sampleScale;
        const float3 sampleDirection = tangent * kernelDirection.x + bitangent * kernelDirection.y + viewNormal * kernelDirection.z;
        const float3 sampleViewPosition = centerViewPosition + sampleDirection * settings.radius;
        const float4 sampleClipPosition = mul(camera.projection, float4(sampleViewPosition, 1.0));

        if (sampleClipPosition.w <= 0.0)
        {
            continue;
        }

        const float2 sampleNdcPosition = sampleClipPosition.xy / sampleClipPosition.w;
        const float2 sampleTextureCoordinate = sampleNdcPosition * 0.5 + 0.5;

        if (any(sampleTextureCoordinate < 0.0) || any(sampleTextureCoordinate >= 1.0))
        {
            continue;
        }

        const uint2 samplePixelCoordinate = min(uint2(sampleTextureCoordinate * float2(textureExtent)), textureExtent - 1);
        const float sampleDepth = depthTexture.Load(int3(samplePixelCoordinate, 0));

        if (sampleDepth >= 1.0)
        {
            continue;
        }

        const float3 sampleWorldPosition = ReconstructWorldPosition(sampleTextureCoordinate, sampleDepth);
        const float3 sampleSurfaceViewPosition = mul(camera.view, float4(sampleWorldPosition, 1.0)).xyz;
        const float depthDifference = abs(centerViewPosition.z - sampleSurfaceViewPosition.z);
        const float rangeWeight = smoothstep(0.0, 1.0, settings.radius / max(depthDifference, 0.0001));
        const float isOccluded = sampleSurfaceViewPosition.z >= sampleViewPosition.z + settings.bias ? 1.0 : 0.0;
        occlusion += isOccluded * rangeWeight;
    }

    const float ambientOcclusion = saturate(1.0 - occlusion / float(SampleCount));
    return pow(ambientOcclusion, max(settings.power, 0.0001));
}