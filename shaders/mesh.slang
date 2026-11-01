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
    float4 materialParams; // Normal scale, unused, unused, unused.
};

struct SceneData
{
    float4x4 viewProjection;
    float4 cameraPosition;
    float4 lightDirection;
    float4 lightColor;
    float4 lighting; // Ambient, specular strength, shininess, unused.
};

[[vk::push_constant]] ConstantBuffer<PushConstants> constants;
[[vk::binding(0, 0)]] Sampler2D albedo;
[[vk::binding(1, 0)]] ConstantBuffer<SceneData> scene;
[[vk::binding(2, 0)]] Sampler2D normalMap;

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
        normal = normalize(tangent * mappedNormal.x + bitangent * mappedNormal.y + normal * mappedNormal.z);
    }
    const float3 lightDirection = normalize(scene.lightDirection.xyz);
    const float diffuse = max(dot(normal, lightDirection), 0.0);
    const float3 toCamera = scene.cameraPosition.xyz - input.worldPosition;
    const float distance = length(toCamera);
    float specular = 0.0;
    if (diffuse > 0.0 && distance > 0.0)
    {
        const float3 halfway = lightDirection + toCamera / distance;
        // Opposite light/view directions have no defined halfway direction.
        if (dot(halfway, halfway) > 1e-6)
            specular = pow(max(dot(normal, normalize(halfway)), 0.0), scene.lighting.z);
    }
    const float4 color = albedo.Sample(input.uv) * constants.baseColorFactor;
    const float3 litColor = color.rgb * (scene.lighting.x + scene.lightColor.rgb * diffuse) +
                            scene.lightColor.rgb * scene.lighting.y * specular;
    return float4(litColor, color.a);
}
