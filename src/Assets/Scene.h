#pragma once

#include <cstdint>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat4x4.hpp>
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

struct SceneNode
{
    uint32_t parent = noParentNode;
    NodeTransform transform;
    std::optional<glm::mat4> matrix;
    std::optional<ModelBounds> meshBounds;
};

enum class AnimationTarget
{
    Translation,
    Rotation,
    Scale
};

enum class AnimationInterpolation
{
    Linear,
    Step,
    CubicSpline
};

struct AnimationTrack
{
    uint32_t node;
    AnimationTarget target;
    AnimationInterpolation interpolation;
    std::vector<float> times;
    std::vector<glm::vec4> values;
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
    std::vector<AnimationClip> clips;
};

struct ModelPose
{
    std::vector<NodeTransform> local;
    std::vector<glm::mat4> world;
    ModelBounds bounds{};
};

void evaluatePose(const ModelScene& scene, const AnimationClip& clip, float time, ModelPose& pose);
