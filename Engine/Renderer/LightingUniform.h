#pragma once

#include "Renderer/EnvironmentLighting.h"

#include <array>
#include <glm/glm.hpp>

namespace Kosmos
{
struct alignas(16) LightingUniform
{
        glm::mat4 directionalLightViewProjection{1.0f};
        glm::vec4 ambient;
        glm::vec4 directionalDirection;
        glm::vec4 directionalColor;
        glm::vec4 pointPosition;
        glm::vec4 pointColor;
        glm::vec4 pointAttenuation;
        glm::vec4 directionalShadowParameters;
        glm::vec4 environmentParameters;
        std::array<glm::vec4, EnvironmentLighting::DiffuseIrradianceCoefficientCount> diffuseIrradianceSH;
    };

    static_assert(sizeof(LightingUniform) == 336);
}