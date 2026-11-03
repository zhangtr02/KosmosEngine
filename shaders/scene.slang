struct SceneData
{
    float4x4 viewProjection;
    float4 cameraPosition;
    float4 lightDirection;
    float4 lightColor;
    float4 lighting; // Diffuse IBL strength, exposure, specular IBL strength, Bloom strength.
    float4x4 inverseSkyViewProjection;
    float4x4 lightViewProjection;
    float4 shadowParams; // Enabled, depth bias, PCF enabled, unused.
    float4 pointPositionRange;
    float4 pointColorIntensity;
    float4x4 pointViewProjections[6];
    float4 pointShadowParams; // Enabled, world-space bias, PCF enabled, filter radius in texels.
    float4 spotDirectionEnabled;
    float4 spotCone; // Inner/outer cosine, 2D shadow enabled, tangent of outer half-angle.
    float4x4 spotViewProjection;
};

[[vk::binding(1, 0)]] ConstantBuffer<SceneData> scene;
