#pragma once

#include <glm/glm.hpp>

namespace Kosmos
{
    struct alignas(16) LightingUniform
    {
        glm::vec4 ambient;
        glm::vec4 directionalDirection;
        glm::vec4 directionalColor;
        glm::vec4 pointPosition;
        glm::vec4 pointColor;
        glm::vec4 pointAttenuation;
    };

    static_assert(sizeof(LightingUniform) == 96);
}