static const float PI = 3.14159265359;

struct CameraUniform
{
    float4x4 view;
    float4x4 projection;
    float4 position;
};

struct MaterialUniform
{
    float4 baseColor;
    float metallic;
    float roughness;
    float ambientOcclusion;
    float emissiveStrength;
};

struct LightingUniform
{
    float4x4 directionalLightViewProjection;
    float4 ambient;
    float4 directionalDirection;
    float4 directionalColor;
    float4 pointPosition;
    float4 pointColor;
    float4 pointAttenuation;
    float4 directionalShadowParameters;
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraUniform> camera : register(b0, space0);

[[vk::binding(1, 0)]]
ConstantBuffer<LightingUniform> lighting : register(b1, space0);

[[vk::combinedImageSampler]]
[[vk::binding(2, 0)]]
Texture2D<float> directionalShadowMap : register(t2, space0);

[[vk::combinedImageSampler]]
[[vk::binding(2, 0)]]
SamplerComparisonState directionalShadowSampler : register(s2, space0);

[[vk::binding(0, 1)]]
ConstantBuffer<MaterialUniform> material : register(b0, space1);

[[vk::combinedImageSampler]]
[[vk::binding(1, 1)]]
Texture2D<float4> baseColorTexture : register(t1, space1);

[[vk::combinedImageSampler]]
[[vk::binding(1, 1)]]
SamplerState baseColorSampler : register(s1, space1);

struct PSInput
{
    [[vk::location(0)]] float3 color : COLOR0;
    [[vk::location(1)]] float2 textureCoordinate : TEXCOORD0;
    [[vk::location(2)]] float3 worldPosition : POSITION0;
    [[vk::location(3)]] float3 worldNormal : NORMAL0;
};

float DistributionGGX(float3 normal, float3 halfDirection, float roughness)
{
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float normalDotHalf = saturate(dot(normal, halfDirection));
    const float normalDotHalfSquared = normalDotHalf * normalDotHalf;
    const float denominator = normalDotHalfSquared * (alphaSquared - 1.0) + 1.0;

    return alphaSquared / max(PI * denominator * denominator, 0.000001);
}

float GeometrySchlickGGX(float normalDotDirection, float roughness)
{
    const float roughnessOffset = roughness + 1.0;
    const float k = roughnessOffset * roughnessOffset / 8.0;

    return normalDotDirection / max(normalDotDirection * (1.0 - k) + k, 0.000001);
}

float GeometrySmith(float3 normal, float3 viewDirection, float3 lightDirection, float roughness)
{
    const float normalDotView = saturate(dot(normal, viewDirection));
    const float normalDotLight = saturate(dot(normal, lightDirection));
    return GeometrySchlickGGX(normalDotView, roughness) * GeometrySchlickGGX(normalDotLight, roughness);
}

float3 FresnelSchlick(float cosine, float3 reflectanceAtNormalIncidence)
{
    return reflectanceAtNormalIncidence + (1.0 - reflectanceAtNormalIncidence) * pow(1.0 - saturate(cosine), 5.0);
}

float3 EvaluateDirectLight(float3 albedo, float metallic, float roughness, float3 normal, float3 viewDirection, float3 lightDirection, float3 radiance)
{
    const float normalDotLight = saturate(dot(normal, lightDirection));

    if (normalDotLight <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }

    const float3 halfDirection = normalize(viewDirection + lightDirection);
    const float normalDistribution = DistributionGGX(normal, halfDirection, roughness);
    const float geometry = GeometrySmith(normal, viewDirection, lightDirection, roughness);
    const float3 baseReflectance = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    const float3 fresnel = FresnelSchlick(saturate(dot(halfDirection, viewDirection)), baseReflectance);

    const float normalDotView = saturate(dot(normal, viewDirection));
    const float denominator = max(4.0 * normalDotView * normalDotLight, 0.0001);
    const float3 specular = normalDistribution * geometry * fresnel / denominator;

    const float3 specularWeight = fresnel;
    const float3 diffuseWeight = (1.0 - specularWeight) * (1.0 - metallic);
    const float3 diffuse = diffuseWeight * albedo / PI;

    return (diffuse + specular) * radiance * normalDotLight;
}

float CalculateDirectionalVisibility(float3 worldPosition, float3 worldNormal, float3 lightDirection)
{
    const float4 lightClipPosition = mul(lighting.directionalLightViewProjection, float4(worldPosition, 1.0));

    if (lightClipPosition.w <= 0.0)
    {
        return 1.0;
    }

    const float3 lightNdcPosition = lightClipPosition.xyz / lightClipPosition.w;

    if (lightNdcPosition.x < -1.0 || lightNdcPosition.x > 1.0 ||
        lightNdcPosition.y < -1.0 || lightNdcPosition.y > 1.0 ||
        lightNdcPosition.z < 0.0 || lightNdcPosition.z > 1.0)
    {
        return 1.0;
    }

    const float2 shadowTextureCoordinate = lightNdcPosition.xy * 0.5 + 0.5;
    const float normalDotLight = saturate(dot(worldNormal, lightDirection));
    const float depthBias = max(lighting.directionalShadowParameters.x, lighting.directionalShadowParameters.y * (1.0 - normalDotLight));
    const float comparisonDepth = lightNdcPosition.z - depthBias;

    uint shadowWidth = 0;
    uint shadowHeight = 0;
    directionalShadowMap.GetDimensions(shadowWidth, shadowHeight);

    const float2 texelSize = 1.0 / float2(shadowWidth, shadowHeight);
    const float filterRadius = lighting.directionalShadowParameters.w;
    float visibility = 0.0;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const float2 offset = float2(x, y) * texelSize * filterRadius;
            visibility += directionalShadowMap.SampleCmpLevelZero(directionalShadowSampler, shadowTextureCoordinate + offset, comparisonDepth);
        }
    }

    visibility /= 9.0;
    return lerp(1.0, visibility, saturate(lighting.directionalShadowParameters.z));
}

float4 main(PSInput input) : SV_TARGET
{
    const float4 textureColor = baseColorTexture.Sample(baseColorSampler, input.textureCoordinate);
    const float4 surfaceColor = textureColor * material.baseColor * float4(input.color, 1.0);
    const float3 albedo = surfaceColor.rgb;
    const float metallic = saturate(material.metallic);
    const float roughness = clamp(material.roughness, 0.045, 1.0);
    const float3 normal = normalize(input.worldNormal);
    const float3 viewDirection = normalize(camera.position.xyz - input.worldPosition);

    const float3 directionalLightDirection = normalize(-lighting.directionalDirection.xyz);
    const float3 directionalRadiance = lighting.directionalColor.rgb * lighting.directionalColor.a;
    const float directionalVisibility = CalculateDirectionalVisibility(input.worldPosition, normal, directionalLightDirection);
    const float3 directionalLighting = EvaluateDirectLight(albedo, metallic, roughness, normal, viewDirection, directionalLightDirection, directionalRadiance) * directionalVisibility;

    const float3 pointOffset = lighting.pointPosition.xyz - input.worldPosition;
    const float pointDistance = max(length(pointOffset), 0.0001);
    const float3 pointLightDirection = pointOffset / pointDistance;
    const float attenuationDenominator = lighting.pointAttenuation.x + lighting.pointAttenuation.y * pointDistance + lighting.pointAttenuation.z * pointDistance * pointDistance;
    const float attenuation = 1.0 / max(attenuationDenominator, 0.0001);
    const float3 pointRadiance = lighting.pointColor.rgb * lighting.pointColor.a * attenuation;
    const float3 pointLighting = EvaluateDirectLight(albedo, metallic, roughness, normal, viewDirection, pointLightDirection, pointRadiance);

    const float3 ambientLighting = lighting.ambient.rgb * lighting.ambient.a * albedo * saturate(material.ambientOcclusion);
    const float3 emissiveLighting = albedo * max(material.emissiveStrength, 0.0);
    const float3 finalColor = ambientLighting + directionalLighting + pointLighting + emissiveLighting;

    return float4(finalColor, surfaceColor.a);
}