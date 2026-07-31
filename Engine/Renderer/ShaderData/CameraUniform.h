#pragma once

#include <glm/glm.hpp>

namespace Kosmos
{
    struct alignas(16) CameraUniform
    {
        glm::mat4 view{1.0f};
        glm::mat4 projection{1.0f};
        glm::mat4 inverseViewProjection{1.0f};
        glm::vec4 position;
    };

    static_assert(sizeof(CameraUniform) == 208);
}