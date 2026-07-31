struct MaterialUniform
{
    float4 baseColor;
    float metallic;
    float roughness;
    float ambientOcclusion;
    float emissiveStrength;
};

[[vk::binding(0, 1)]]
ConstantBuffer<MaterialUniform> material : register(b0, space1);

[[vk::combinedImageSampler]]
[[vk::binding(1, 1)]]
Texture2D<float4> baseColorTexture : register(t1, space1);

[[vk::combinedImageSampler]]
[[vk::binding(1, 1)]]
SamplerState baseColorSampler : register(s1, space1);

[[vk::combinedImageSampler]]
[[vk::binding(2, 1)]]
Texture2D<float4> ormTexture : register(t2, space1);

[[vk::combinedImageSampler]]
[[vk::binding(2, 1)]]
SamplerState ormSampler : register(s2, space1);

[[vk::combinedImageSampler]]
[[vk::binding(3, 1)]]
Texture2D<float4> normalTexture : register(t3, space1);

[[vk::combinedImageSampler]]
[[vk::binding(3, 1)]]
SamplerState normalSampler : register(s3, space1);

struct PSInput
{
    [[vk::location(0)]] float3 color : COLOR0;
    [[vk::location(1)]] float2 textureCoordinate : TEXCOORD0;
    [[vk::location(2)]] float3 worldNormal : NORMAL0;
    [[vk::location(3)]] float4 worldTangent : TANGENT0;
};

struct PSOutput
{
    float4 albedoAmbientOcclusion : SV_TARGET0;
    float4 normalRoughness : SV_TARGET1;
    float4 materialParameters : SV_TARGET2;
};

float3 CalculateWorldNormal(PSInput input)
{
    const float3 geometryNormal = normalize(input.worldNormal);
    float3 tangent = input.worldTangent.xyz - geometryNormal * dot(geometryNormal, input.worldTangent.xyz);
    tangent = normalize(tangent);

    const float handedness = input.worldTangent.w < 0.0 ? -1.0 : 1.0;
    const float3 bitangent = normalize(cross(geometryNormal, tangent)) * handedness;
    const float3 tangentNormal = normalTexture.Sample(normalSampler, input.textureCoordinate).xyz * 2.0 - 1.0;
    return normalize(tangent * tangentNormal.x + bitangent * tangentNormal.y + geometryNormal * tangentNormal.z);
}

PSOutput main(PSInput input)
{
    const float4 textureColor = baseColorTexture.Sample(baseColorSampler, input.textureCoordinate);
    const float4 orm = ormTexture.Sample(ormSampler, input.textureCoordinate);
    const float4 surfaceColor = textureColor * material.baseColor * float4(input.color, 1.0);
    const float metallic = saturate(material.metallic * orm.b);
    const float roughness = clamp(material.roughness * orm.g, 0.045, 1.0);
    const float ambientOcclusion = saturate(material.ambientOcclusion * orm.r);
    const float3 worldNormal = CalculateWorldNormal(input);

    PSOutput output;
    output.albedoAmbientOcclusion = float4(surfaceColor.rgb, ambientOcclusion);
    output.normalRoughness = float4(worldNormal, roughness);
    output.materialParameters = float4(metallic, max(material.emissiveStrength, 0.0), surfaceColor.a, 0.0);
    return output;
}