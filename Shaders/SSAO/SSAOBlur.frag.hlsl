static const int BlurRadius = 2;

struct CameraUniform
{
    float4x4 view;
    float4x4 projection;
    float4x4 inverseViewProjection;
    float4 position;
};

struct SSAOBlurPushConstant
{
    int2 direction;
    float depthSharpness;
    float normalSharpness;
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraUniform> camera : register(b0, space0);

[[vk::binding(0, 1)]]
Texture2D<float> ambientOcclusionTexture : register(t0, space1);

[[vk::binding(1, 1)]]
Texture2D<float4> normalRoughnessTexture : register(t1, space1);

[[vk::binding(2, 1)]]
Texture2D<float> depthTexture : register(t2, space1);

[[vk::push_constant]]
ConstantBuffer<SSAOBlurPushConstant> settings;

struct PSInput
{
    float4 position : SV_POSITION;
    [[vk::location(0)]] float2 textureCoordinate : TEXCOORD0;
};

float3 ReconstructViewPosition(float2 textureCoordinate, float depth)
{
    const float2 ndcPosition = textureCoordinate * 2.0 - 1.0;
    const float4 worldPositionHomogeneous = mul(camera.inverseViewProjection, float4(ndcPosition, depth, 1.0));
    const float3 worldPosition = worldPositionHomogeneous.xyz / worldPositionHomogeneous.w;
    return mul(camera.view, float4(worldPosition, 1.0)).xyz;
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
    const float centerAmbientOcclusion = ambientOcclusionTexture.Load(int3(pixelCoordinate, 0));
    const float3 centerNormal = normalize(normalRoughnessTexture.Load(int3(pixelCoordinate, 0)).xyz);
    const float3 centerViewPosition = ReconstructViewPosition(input.textureCoordinate, centerDepth);
    float weightedAmbientOcclusion = 0.0;
    float totalWeight = 0.0;

    [unroll]
    for (int offset = -BlurRadius; offset <= BlurRadius; ++offset)
    {
        const int2 unclampedCoordinate = int2(pixelCoordinate) + settings.direction * offset;
        const int2 clampedCoordinate = clamp(unclampedCoordinate, int2(0, 0), int2(textureExtent) - 1);
        const uint2 sampleCoordinate = uint2(clampedCoordinate);
        const float sampleDepth = depthTexture.Load(int3(sampleCoordinate, 0));

        if (sampleDepth >= 1.0)
        {
            continue;
        }

        const float sampleAmbientOcclusion = ambientOcclusionTexture.Load(int3(sampleCoordinate, 0));
        const float3 sampleNormal = normalize(normalRoughnessTexture.Load(int3(sampleCoordinate, 0)).xyz);
        const float2 sampleTextureCoordinate = (float2(sampleCoordinate) + 0.5) / float2(textureExtent);
        const float3 sampleViewPosition = ReconstructViewPosition(sampleTextureCoordinate, sampleDepth);
        const float normalizedOffset = float(offset) / float(BlurRadius);
        const float spatialWeight = exp(-0.5 * normalizedOffset * normalizedOffset);
        const float depthWeight = exp(-abs(centerViewPosition.z - sampleViewPosition.z) * max(settings.depthSharpness, 0.0));
        const float normalWeight = pow(saturate(dot(centerNormal, sampleNormal)), max(settings.normalSharpness, 0.0));
        const float weight = spatialWeight * depthWeight * normalWeight;
        weightedAmbientOcclusion += sampleAmbientOcclusion * weight;
        totalWeight += weight;
    }

    return totalWeight > 0.0001 ? weightedAmbientOcclusion / totalWeight : centerAmbientOcclusion;
}