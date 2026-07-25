#pragma once

#include <glm/glm.hpp>

namespace Kosmos
{
    struct alignas(16) MaterialUniform
    {
        glm::vec4 baseColor{1.0f};
    };

    static_assert(sizeof(MaterialUniform) == 16);
}