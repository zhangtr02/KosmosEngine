#pragma once

#include <glm/glm.hpp>

namespace Kosmos
{
    struct alignas(16) ObjectPushConstant
    {
        glm::mat4 model{1.0f};
        glm::mat4 normalMatrix{1.0f};
    };

    static_assert(sizeof(ObjectPushConstant) == 128);
}