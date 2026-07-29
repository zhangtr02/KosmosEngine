struct CameraUniform
{
    float4x4 view;
    float4x4 projection;
    float4 position;
};

struct ObjectPushConstant
{
    float4x4 model;
    float4x4 normalMatrix;
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraUniform> camera : register(b0, space0);

[[vk::push_constant]]
ConstantBuffer<ObjectPushConstant> objectPushConstant;

struct VSInput
{
    [[vk::location(0)]] float3 position : POSITION0;
    [[vk::location(1)]] float3 color : COLOR0;
    [[vk::location(2)]] float2 textureCoordinate : TEXCOORD0;
    [[vk::location(3)]] float3 normal : NORMAL0;
    [[vk::location(4)]] float4 tangent : TANGENT0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    [[vk::location(0)]] float3 color : COLOR0;
    [[vk::location(1)]] float2 textureCoordinate : TEXCOORD0;
    [[vk::location(2)]] float3 worldPosition : POSITION0;
    [[vk::location(3)]] float3 worldNormal : NORMAL0;
    [[vk::location(4)]] float4 worldTangent : TANGENT0;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    const float4 localPosition = float4(input.position, 1.0);
    const float4 worldPosition = mul(objectPushConstant.model, localPosition);
    const float3 worldNormal = normalize(mul((float3x3)objectPushConstant.normalMatrix, input.normal));
    float3 worldTangent = mul((float3x3)objectPushConstant.model, input.tangent.xyz);
    worldTangent = normalize(worldTangent - worldNormal * dot(worldNormal, worldTangent));

    const float modelHandedness = determinant((float3x3)objectPushConstant.model) < 0.0 ? -1.0 : 1.0;

    output.position = mul(camera.projection, mul(camera.view, worldPosition));
    output.color = input.color;
    output.textureCoordinate = input.textureCoordinate;
    output.worldPosition = worldPosition.xyz;
    output.worldNormal = worldNormal;
    output.worldTangent = float4(worldTangent, input.tangent.w * modelHandedness);

    return output;
}