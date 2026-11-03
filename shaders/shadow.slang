#include "scene.slang"
#include "material.slang"

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain([[vk::location(0)]] float3 position : POSITION,
                        [[vk::location(1)]] float2 uv : TEXCOORD0)
{
    VertexOutput output;
    output.position = mul(scene.lightViewProjection, mul(constants.model, float4(position, 1.0)));
    output.uv = uv;
    return output;
}

[shader("fragment")]
void fragmentMain(VertexOutput input)
{
    if (constants.surfaceParams.x >= 0.0)
        alphaTest(albedo.Sample(input.uv).a * constants.baseColorFactor.a);
}
