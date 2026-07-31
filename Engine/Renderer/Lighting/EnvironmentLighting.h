#pragma once

#include <glm/glm.hpp>
#include <array>

namespace Kosmos
{
    class CubeTexture;

    namespace EnvironmentLighting
    {
        constexpr uint32_t DiffuseIrradianceCoefficientCount = 9;

        std::array<glm::vec4, DiffuseIrradianceCoefficientCount> ProjectDiffuseIrradiance(const CubeTexture& environment);
    }
}