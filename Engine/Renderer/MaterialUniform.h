#pragma once

#include <glm/glm.hpp>

namespace Kosmos
{
    struct alignas(16) MaterialUniform
    {
        glm::vec4 baseColor{1.0f};
        float metallic = 0.0f;
        float roughness = 0.5f;
        float ambientOcclusion = 1.0f;
        float emissiveStrength = 0.0f;
    };

    static_assert(sizeof(MaterialUniform) == 32);
}