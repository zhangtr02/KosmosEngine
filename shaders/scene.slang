struct SceneData
{
    float4x4 viewProjection;
    float4 cameraPosition;
    float4 lightDirection;
    float4 lightColor;
    float4 lighting; // Diffuse IBL strength, exposure, specular IBL strength, unused.
    float4x4 inverseSkyViewProjection;
};

[[vk::binding(1, 0)]] ConstantBuffer<SceneData> scene;
