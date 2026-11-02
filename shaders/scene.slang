struct SceneData
{
    float4x4 viewProjection;
    float4 cameraPosition;
    float4 lightDirection;
    float4 lightColor;
    float4 lighting; // Diffuse IBL strength, exposure, unused, unused.
    float4x4 inverseSkyViewProjection;
};

[[vk::binding(1, 0)]] ConstantBuffer<SceneData> scene;

float3 toneMap(float3 radiance)
{
    const float3 exposed = radiance * scene.lighting.y;
    // Reinhard; the sRGB attachment performs the output transfer function.
    return exposed / (1.0 + exposed);
}
