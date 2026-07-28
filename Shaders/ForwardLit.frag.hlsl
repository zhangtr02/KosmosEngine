struct MaterialUniform
{
    float4 baseColor;
};

struct LightingUniform
{
    float4 ambient;
    float4 directionalDirection;
    float4 directionalColor;
    float4 pointPosition;
    float4 pointColor;
    float4 pointAttenuation;
};

[[vk::binding(1, 0)]]
ConstantBuffer<LightingUniform> lighting : register(b1, space0);

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

float4 main(PSInput input) : SV_TARGET
{
    const float4 textureColor = baseColorTexture.Sample(baseColorSampler, input.textureCoordinate);
    const float4 surfaceColor = textureColor * material.baseColor * float4(input.color, 1.0);

    const float3 normal = normalize(input.worldNormal);

    const float3 directionalLightDirection = normalize(-lighting.directionalDirection.xyz);
    const float directionalDiffuse = max(dot(normal, directionalLightDirection), 0.0);
    const float3 directionalLighting = lighting.directionalColor.rgb * lighting.directionalColor.a * directionalDiffuse;

    const float3 pointOffset = lighting.pointPosition.xyz - input.worldPosition;
    const float pointDistance = max(length(pointOffset), 0.0001);
    const float3 pointDirection = pointOffset / pointDistance;
    const float pointDiffuse = max(dot(normal, pointDirection), 0.0);
    const float attenuationDenominator = lighting.pointAttenuation.x + lighting.pointAttenuation.y * pointDistance + lighting.pointAttenuation.z * pointDistance * pointDistance;
    const float attenuation = 1.0 / max(attenuationDenominator, 0.0001);
    const float3 pointLighting = lighting.pointColor.rgb * lighting.pointColor.a * pointDiffuse * attenuation;

    const float3 ambientLighting = lighting.ambient.rgb * lighting.ambient.a;
    const float3 finalLighting = ambientLighting + directionalLighting + pointLighting;

    return float4(surfaceColor.rgb * finalLighting, surfaceColor.a);
}