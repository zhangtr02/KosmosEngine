#pragma once

#include <array>
#include <cstdint>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <optional>
#include <string>
#include <vector>

struct ModelBounds
{
    glm::vec3 minimum;
    glm::vec3 maximum;
};

struct NodeTransform
{
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};

    glm::mat4 matrix() const;
};

inline constexpr uint32_t noParentNode = UINT32_MAX;

enum MaterialTextureSlot
{
    BaseColorSlot,
    NormalSlot,
    MetallicRoughnessSlot,
    OcclusionSlot,
    EmissiveSlot,
    BaseTextureCount,
    SpecularSlot = BaseTextureCount,
    SpecularColorSlot,
    ClearcoatSlot,
    ClearcoatRoughnessSlot,
    ClearcoatNormalSlot,
    SheenColorSlot,
    SheenRoughnessSlot,
    AnisotropySlot,
    IridescenceSlot,
    IridescenceThicknessSlot,
    MaterialTextureCount
};

inline constexpr size_t advancedUvPairs = (MaterialTextureCount - BaseTextureCount) / 2;

struct TextureTransform
{
    glm::vec2 offset{0.0f};
    glm::vec2 scale{1.0f};
    float rotation = 0.0f;
};

struct AdvancedMaterial
{
    glm::vec4 specular{1.0f};                          // RGB color, strength.
    glm::vec4 clearcoat{0.0f};                         // Strength, roughness, normal scale, unused.
    glm::vec4 sheen{0.0f};                             // RGB color, roughness.
    glm::vec4 anisotropy{0.0f, 1.0f, 0.0f, 0.0f};      // Strength, cos, sin, has texture.
    glm::vec4 iridescence{0.0f, 1.3f, 100.0f, 400.0f}; // Strength, IOR, min/max nm.
    glm::vec4 workflow{1.5f, 1.0f, 0.0f, 0.0f};        // IOR, glossiness, legacy workflow, unused.
};

struct MaterialValues
{
    glm::vec4 baseColorFactor{1.0f};
    glm::vec3 emissiveFactor{0.0f};
    float emissiveStrength = 1.0f;
    float normalScale = 0.0f;
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float occlusionStrength = 1.0f;
    float alphaCutoff = 0.5f;
    std::array<TextureTransform, MaterialTextureCount> uv;
    AdvancedMaterial advanced;
    uint32_t gpuIndex = 0;
    bool animatedUv = false;
};

struct SceneNode
{
    uint32_t parent = noParentNode;
    NodeTransform transform;
    std::optional<glm::mat4> matrix;
    std::optional<ModelBounds> meshBounds;
    bool visible = true;
    uint32_t morphOffset{};
    std::vector<ModelBounds> morphBounds;
    uint32_t morphSkin{};
    uint32_t morphSkinJointCount{};
};

struct SkinJoint
{
    uint32_t node;
    glm::mat4 inverseBind{1.0f};
    std::optional<ModelBounds> meshBounds;
};

enum class AnimationTarget
{
    Translation,
    Rotation,
    Scale,
    Weights,
    Weight,
    Visibility,
    BaseColor,
    Metallic,
    Roughness,
    Emissive,
    EmissiveStrength,
    NormalScale,
    OcclusionStrength,
    AlphaCutoff,
    TextureOffset,
    TextureScale,
    TextureRotation
};

struct AnimationBinding
{
    AnimationTarget target;
    uint32_t index;
    uint32_t element = 0; // Texture slot or individual morph target.
};

enum class AnimationInterpolation
{
    Linear,
    Step,
    CubicSpline
};

struct AnimationTrack
{
    AnimationBinding binding;
    AnimationInterpolation interpolation;
    std::vector<float> times;
    std::vector<glm::vec4> values;
    std::vector<float> weightValues;
};

struct AnimationClip
{
    std::string name;
    float duration = 0.0f;
    std::vector<AnimationTrack> tracks;
};

struct ModelScene
{
    std::vector<SceneNode> nodes; // Parents precede their children.
    std::vector<glm::mat4> restWorld;
    std::vector<uint8_t> restVisibility;
    std::vector<AnimationClip> clips;
    std::vector<SkinJoint> joints;
    std::vector<glm::mat4> restJoints;
    std::vector<float> restWeights;
    std::vector<MaterialValues> restMaterials;
    uint32_t gpuMaterialCount = 0;
};

struct ModelPose
{
    std::vector<NodeTransform> local;
    std::vector<glm::mat4> world;
    std::vector<uint8_t> visibility;
    std::vector<glm::mat4> joints;
    std::vector<float> morphWeights;
    std::vector<MaterialValues> materials;
    ModelBounds bounds{};
};

void evaluatePose(const ModelScene& scene, const AnimationClip& clip, float time, ModelPose& pose);
