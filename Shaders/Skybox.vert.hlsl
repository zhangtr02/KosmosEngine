struct CameraUniform
{
    float4x4 view;
    float4x4 projection;
    float4 position;
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraUniform> camera : register(b0, space0);

struct VSOutput
{
    float4 position : SV_POSITION;
    [[vk::location(0)]] float3 direction : TEXCOORD0;
};

VSOutput main(uint vertexIndex : SV_VertexID)
{
    const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(3.0, -1.0),
        float2(-1.0, 3.0)
    };

    VSOutput output;
    const float2 ndcPosition = positions[vertexIndex];

    const float3 viewDirection = normalize(float3(
        ndcPosition.x / camera.projection[0][0],
        ndcPosition.y / camera.projection[1][1],
        -1.0));

    output.position = float4(ndcPosition, 0.0, 1.0);
    output.direction = normalize(mul(transpose((float3x3)camera.view), viewDirection));
    return output;
}