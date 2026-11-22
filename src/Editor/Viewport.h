#pragma once

#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <optional>

struct ViewportRay
{
    glm::vec3 origin;
    glm::vec3 direction;
    float nearDistance;
};

struct EditorViewport
{
    glm::mat4 view;
    glm::vec2 origin;
    glm::vec2 size;
    float aspect;
    float tanHalfFov;
    float nearPlane;
    float uiScale;

    std::optional<glm::vec2> project(glm::vec3 position) const
    {
        const glm::vec3 camera(view * glm::vec4(position, 1.0f));
        if (-camera.z <= nearPlane)
            return {};
        const auto ndc = glm::vec2(camera.x / aspect, -camera.y) / (-camera.z * tanHalfFov);
        return origin + (ndc * 0.5f + 0.5f) * size;
    }

    ViewportRay ray(glm::vec2 mouse) const
    {
        const auto ndc = (mouse - origin) / size * 2.0f - 1.0f;
        const auto inverse = glm::inverse(view);
        const glm::vec3 direction(ndc.x * aspect * tanHalfFov, -ndc.y * tanHalfFov, -1.0f);
        return {glm::vec3(inverse[3]), glm::normalize(glm::mat3(inverse) * direction),
                nearPlane * glm::length(direction)};
    }
};
