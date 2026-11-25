#include "Scene.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>

void includeBounds(ModelBounds& result, const ModelBounds& bounds, const glm::mat4& transform)
{
    const auto center = (bounds.minimum + bounds.maximum) * 0.5f;
    const auto extent = (bounds.maximum - bounds.minimum) * 0.5f;
    const auto transformedCenter = glm::vec3(transform * glm::vec4(center, 1.0f));
    const auto transformedExtent = glm::abs(glm::vec3(transform[0])) * extent.x +
                                   glm::abs(glm::vec3(transform[1])) * extent.y +
                                   glm::abs(glm::vec3(transform[2])) * extent.z;
    result.minimum = glm::min(result.minimum, transformedCenter - transformedExtent);
    result.maximum = glm::max(result.maximum, transformedCenter + transformedExtent);
}

namespace
{
glm::quat quaternion(glm::vec4 value) { return {value.w, value.x, value.y, value.z}; }

struct Segment
{
    size_t first, second;
    float t = 0.0f, interval = 0.0f;
};

Segment findSegment(const AnimationTrack& track, float time)
{
    const auto& times = track.times;
    const size_t next =
        static_cast<size_t>(std::upper_bound(times.begin(), times.end(), time) - times.begin());
    if (!next)
        return {0, 0};
    const auto first = next - 1;
    if (next == times.size() || track.interpolation == AnimationInterpolation::Step)
        return {first, first};
    const float interval = times[next] - times[first];
    return {first, next, (time - times[first]) / interval, interval};
}

template <typename Read, typename Linear>
auto sampleTrack(const AnimationTrack& track, const Segment& key, Read read, Linear linear)
{
    const bool cubic = track.interpolation == AnimationInterpolation::CubicSpline;
    const auto a = read(cubic ? key.first * 3 + 1 : key.first);
    if (key.first == key.second)
        return a;
    const auto b = read(cubic ? key.second * 3 + 1 : key.second);
    if (cubic)
    {
        const float t = key.t;
        const float t2 = t * t, t3 = t2 * t;
        return (2 * t3 - 3 * t2 + 1) * a +
               (t3 - 2 * t2 + t) * key.interval * read(key.first * 3 + 2) + (-2 * t3 + 3 * t2) * b +
               (t3 - t2) * key.interval * read(key.second * 3);
    }
    return linear(a, b, key.t);
}
} // namespace

glm::mat4 NodeTransform::matrix() const
{
    return glm::scale(glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation),
                      scale);
}

void evaluatePose(const ModelScene& scene, const AnimationClip& clip, float time, ModelPose& pose)
{
    pose.local.resize(scene.nodes.size());
    pose.world.resize(scene.nodes.size());
    pose.visibility.resize(scene.nodes.size());
    pose.morphWeights = scene.restWeights;
    pose.materials = scene.restMaterials;
    for (size_t i = 0; i < scene.nodes.size(); ++i)
    {
        pose.local[i] = scene.nodes[i].transform;
        pose.visibility[i] = scene.nodes[i].visible;
    }
    for (const auto& track : clip.tracks)
    {
        const auto key = findSegment(track, time);
        const auto [target, index, element] = track.binding;
        if (target == AnimationTarget::Weights)
        {
            const auto& node = scene.nodes[index];
            const auto count = node.morphBounds.size();
            for (size_t weight = 0; weight < count; ++weight)
                pose.morphWeights[node.morphOffset + weight] = sampleTrack(
                    track, key,
                    [&](size_t i)
                    {
                        return track.weightValues[i * count + weight];
                    },
                    [](float a, float b, float t)
                    {
                        return glm::mix(a, b, t);
                    });
            continue;
        }
        const auto value = sampleTrack(
            track, key,
            [&](size_t i)
            {
                return track.values[i];
            },
            [&](glm::vec4 a, glm::vec4 b, float t)
            {
                if (target != AnimationTarget::Rotation)
                    return glm::mix(a, b, t);
                const auto q = glm::slerp(quaternion(a), quaternion(b), t);
                return glm::vec4(q.x, q.y, q.z, q.w);
            });
        switch (target)
        {
        case AnimationTarget::Translation:
            pose.local[index].translation = glm::vec3(value);
            break;
        case AnimationTarget::Rotation:
            if (const float length = glm::length(value); std::isfinite(length) && length > 0.0f)
                pose.local[index].rotation = quaternion(value / length);
            break;
        case AnimationTarget::Scale:
            pose.local[index].scale = glm::vec3(value);
            break;
        case AnimationTarget::Visibility:
            pose.visibility[index] = value.x != 0.0f;
            break;
        case AnimationTarget::BaseColor:
            pose.materials[index].baseColorFactor = glm::clamp(value, 0.0f, 1.0f);
            break;
        case AnimationTarget::TextureOffset:
            pose.materials[index].uv[element].offset = glm::vec2(value);
            break;
        case AnimationTarget::TextureScale:
            pose.materials[index].uv[element].scale = glm::vec2(value);
            break;
        case AnimationTarget::TextureRotation:
            pose.materials[index].uv[element].rotation = value.x;
            break;
        case AnimationTarget::Weights:
            break;
        }
    }

    const glm::vec3 limit(std::numeric_limits<float>::max());
    pose.bounds = {limit, -limit};
    for (size_t i = 0; i < scene.nodes.size(); ++i)
    {
        const auto& node = scene.nodes[i];
        const auto local = node.matrix ? *node.matrix : pose.local[i].matrix();
        auto& world = pose.world[i];
        world = node.parent == noParentNode ? local : pose.world[node.parent] * local;
        if (node.parent != noParentNode)
            pose.visibility[i] &= pose.visibility[node.parent];
        if (node.meshBounds && node.morphBounds.empty())
            includeBounds(pose.bounds, *node.meshBounds, world);
    }
    pose.joints.resize(scene.joints.size());
    for (size_t i = 0; i < scene.joints.size(); ++i)
    {
        const auto& joint = scene.joints[i];
        pose.joints[i] = pose.world[joint.node] * joint.inverseBind;
        // The weighted position lies inside the union of its joint-transformed bounds.
        if (joint.meshBounds)
            includeBounds(pose.bounds, *joint.meshBounds, pose.joints[i]);
    }
    for (size_t i = 0; i < scene.nodes.size(); ++i)
    {
        const auto& node = scene.nodes[i];
        if (node.morphBounds.empty())
            continue;
        auto bounds = *node.meshBounds;
        for (size_t target = 0; target < node.morphBounds.size(); ++target)
        {
            const float weight = pose.morphWeights[node.morphOffset + target];
            const auto a = weight * node.morphBounds[target].minimum;
            const auto b = weight * node.morphBounds[target].maximum;
            bounds.minimum += glm::min(a, b);
            bounds.maximum += glm::max(a, b);
        }
        includeBounds(pose.bounds, bounds, pose.world[i]);
    }
}
