struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float3 normal : NORMAL;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
};

struct PushConstants
{
    float4x4 mvp;
    float4x4 model;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> constants;
[[vk::binding(0, 0)]] Sampler2D albedo;

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(constants.mvp, float4(input.position, 1.0));
    output.uv = input.uv;
    // Valid for rotation and uniform scale; nonuniform scale needs inverse-transpose.
    output.worldNormal = mul((float3x3)constants.model, input.normal);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    const float3 normal = normalize(input.worldNormal);
    const float3 lightDirection = normalize(float3(1.0, 2.0, 3.0)); // Surface to light, world space.
    const float ambient = 0.15;
    const float3 lightColor = float3(0.85, 0.85, 0.85);
    const float diffuse = max(dot(normal, lightDirection), 0.0);
    const float4 color = albedo.Sample(input.uv);
    return float4(color.rgb * (ambient + lightColor * diffuse), color.a);
}
