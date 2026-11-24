#include "Scene.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>

namespace
{
glm::quat quaternion(glm::vec4 value) { return {value.w, value.x, value.y, value.z}; }

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

glm::vec4 sampleTrack(const AnimationTrack& track, float time)
{
    const auto& times = track.times;
    const auto& values = track.values;
    const bool cubic = track.interpolation == AnimationInterpolation::CubicSpline;
    const size_t stride = cubic ? 3 : 1, valueOffset = cubic ? 1 : 0;
    const auto key = [&](size_t i)
    {
        return values[i * stride + valueOffset];
    };
    const size_t next =
        static_cast<size_t>(std::upper_bound(times.begin(), times.end(), time) - times.begin());
    if (next == 0)
        return key(0);
    const auto previous = next - 1;
    const auto a = key(previous);
    if (next == times.size() || track.interpolation == AnimationInterpolation::Step)
        return a;

    const auto b = key(next);
    const float interval = times[next] - times[previous];
    const float t = (time - times[previous]) / interval;
    if (cubic)
    {
        const float t2 = t * t, t3 = t2 * t;
        return (2 * t3 - 3 * t2 + 1) * a + (t3 - 2 * t2 + t) * interval * values[previous * 3 + 2] +
               (-2 * t3 + 3 * t2) * b + (t3 - t2) * interval * values[next * 3];
    }
    if (track.target == AnimationTarget::Rotation)
    {
        const auto rotation =
            glm::slerp(glm::normalize(quaternion(a)), glm::normalize(quaternion(b)), t);
        return {rotation.x, rotation.y, rotation.z, rotation.w};
    }
    return glm::mix(a, b, t);
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
    for (size_t i = 0; i < scene.nodes.size(); ++i)
        pose.local[i] = scene.nodes[i].transform;
    for (const auto& track : clip.tracks)
    {
        const auto value = sampleTrack(track, time);
        auto& transform = pose.local[track.node];
        switch (track.target)
        {
        case AnimationTarget::Translation:
            transform.translation = glm::vec3(value);
            break;
        case AnimationTarget::Rotation:
            if (const float length = glm::length(value); std::isfinite(length) && length > 0.0f)
                transform.rotation = quaternion(value / length);
            break;
        case AnimationTarget::Scale:
            transform.scale = glm::vec3(value);
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
        if (node.meshBounds)
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
}
