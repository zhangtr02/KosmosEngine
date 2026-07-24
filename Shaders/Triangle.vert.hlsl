struct CameraUniform
{
    float4x4 view;
    float4x4 projection;
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraUniform> camera : register(b0, space0);

struct VSInput
{
    [[vk::location(0)]] float2 position : POSITION0;
    [[vk::location(1)]] float3 color : COLOR0;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    [[vk::location(0)]] float3 color : COLOR0;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    const float4 worldPosition = float4(input.position, 0.0, 1.0);

    output.position = mul(camera.projection, mul(camera.view, worldPosition));
    output.color = input.color;
    
    return output;
}