struct ShadowUniform
{
    float4x4 lightViewProjection;
};

struct ShadowPushConstant
{
    float4x4 model;
};

[[vk::binding(1, 0)]]
ConstantBuffer<ShadowUniform> shadow : register(b1, space0);

[[vk::push_constant]]
ConstantBuffer<ShadowPushConstant> objectPushConstant;

struct VSInput
{
    [[vk::location(0)]] float3 position : POSITION0;
};

float4 main(VSInput input) : SV_POSITION
{
    const float4 worldPosition = mul(objectPushConstant.model, float4(input.position, 1.0));
    return mul(shadow.lightViewProjection, worldPosition);
}