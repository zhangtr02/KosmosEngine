#include "scene.slang"

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float3 normal : NORMAL;
    [[vk::location(3)]] float4 tangent : TANGENT;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
    float3 worldPosition : TEXCOORD2;
    float4 worldTangent : TEXCOORD3;
};

struct PushConstants
{
    float4x4 model;
    float4 baseColorFactor;
    float4 materialParams; // Normal scale, metallic factor, roughness factor, unused.
};

[[vk::push_constant]] ConstantBuffer<PushConstants> constants;
[[vk::binding(0, 0)]] Sampler2D albedo;
[[vk::binding(2, 0)]] Sampler2D normalMap;
[[vk::binding(3, 0)]] Sampler2D metallicRoughnessMap;

static const float PI = 3.14159265;

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

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    const float4 worldPosition = mul(constants.model, float4(input.position, 1.0));
    output.position = mul(scene.viewProjection, worldPosition);
    output.worldPosition = worldPosition.xyz;
    output.uv = input.uv;
    // Valid for rotation and uniform scale; nonuniform scale needs inverse-transpose.
    output.worldNormal = mul((float3x3)constants.model, input.normal);
    output.worldTangent = float4(mul((float3x3)constants.model, input.tangent.xyz), input.tangent.w);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    float3 normal = normalize(input.worldNormal);
    if (constants.materialParams.x != 0.0)
    {
        float3 mappedNormal = normalMap.Sample(input.uv).xyz * 2.0 - 1.0;
        mappedNormal.xy *= constants.materialParams.x;
        const float3 tangent = normalize(input.worldTangent.xyz -
                                         normal * dot(normal, input.worldTangent.xyz));
        const float3 bitangent = cross(normal, tangent) * input.worldTangent.w;
        normal = normalize(tangent * mappedNormal.x + bitangent * mappedNormal.y +
                           normal * mappedNormal.z);
    }
    const float4 color = albedo.Sample(input.uv) * constants.baseColorFactor;
    const float2 mr = metallicRoughnessMap.Sample(input.uv).gb;
    const float metallic = saturate(mr.y * constants.materialParams.y);
    const float roughness = clamp(mr.x * constants.materialParams.z, 0.045, 1.0);
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), color.rgb, metallic);
    float3 radiance = color.rgb * (1.0 - metallic) * scene.lighting.x;
    const float3 lightDirection = normalize(scene.lightDirection.xyz);
    const float nDotL = saturate(dot(normal, lightDirection));
    const float3 toCamera = scene.cameraPosition.xyz - input.worldPosition;
    const float distance = length(toCamera);
    if (nDotL > 0.0 && distance > 0.0)
    {
        const float3 viewDirection = toCamera / distance;
        const float nDotV = saturate(dot(normal, viewDirection));
        const float3 halfway = lightDirection + viewDirection;
        // Opposite light/view directions have no defined halfway direction.
        if (nDotV > 0.0 && dot(halfway, halfway) > 1e-6)
        {
            const float3 h = normalize(halfway);
            const float nDotH = saturate(dot(normal, h));
            const float vDotH = saturate(dot(viewDirection, h));
            const float3 f = fresnelSchlick(vDotH, f0);
            const float d = distributionGGX(nDotH, alphaSquared);
            const float visibility = visibilitySmith(nDotL, nDotV, alphaSquared);
            const float3 diffuse = (1.0 - f) * (1.0 - metallic) * color.rgb / PI;
            const float3 specular = d * visibility * f;
            radiance += (diffuse + specular) * scene.lightColor.rgb * nDotL;
        }
    }
    return float4(toneMap(radiance), color.a);
}
