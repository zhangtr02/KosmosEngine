struct VSOutput
{
    float4 position : SV_POSITION;
    [[vk::location(0)]] float2 textureCoordinate : TEXCOORD0;
};

VSOutput main(uint vertexIndex : SV_VertexID)
{
    VSOutput output;

    const float2 textureCoordinate = float2((vertexIndex << 1) & 2, vertexIndex & 2);
    output.position = float4(textureCoordinate * 2.0 - 1.0, 0.0, 1.0);
    output.textureCoordinate = textureCoordinate;

    return output;
}