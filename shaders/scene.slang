static const uint MaxPunctualLights = 8;
static const float SpotLight = 1.0;
static const float DirectionalLight = 2.0;

struct LightData
{
    float4 positionRange;
    float4 colorIntensity;
    float4 directionType;
    float4 cone; // Inner/outer cosine, minimum squared distance, unused W.
};

struct SceneData
{
    float4x4 viewProjection;
    float4 cameraPosition;
    float4 lighting; // Diffuse IBL strength, exposure, specular IBL strength, Bloom strength.
    float4x4 inverseSkyViewProjection;
    float4x4 lightViewProjection;
    float4 shadowParams; // Directional light index (-1 disables), depth bias, PCF, unused.
    LightData lights[MaxPunctualLights];
    float4 localShadowParams; // Light index (-1 disables), world-space bias, PCF, radius.
    float4 localShadowProjection; // 2D enabled, tangent of outer half-angle, unused ZW.
    float4x4 pointViewProjections[6];
    float4x4 spotViewProjection;
};

[[vk::binding(1, 0)]] ConstantBuffer<SceneData> scene;
