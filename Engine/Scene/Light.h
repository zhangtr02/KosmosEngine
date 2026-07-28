#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace Kosmos
{
    struct DirectionalLight
    {
        glm::vec3 direction{-0.5f, -1.0f, -0.3f};
        glm::vec3 color{1.0f};
        float intensity = 1.0f;

        glm::vec3 shadowCenter{0.0f};
        float shadowHalfExtent = 6.0f;
        float shadowDistance = 12.0f;
        float shadowNearPlane = 0.1f;
        float shadowFarPlane = 30.0f;
        uint32_t shadowMapResolution = 2048;
    };

    struct PointLight
    {
        glm::vec3 position{0.0f};
        glm::vec3 color{1.0f};
        float intensity = 1.0f;
        float constantAttenuation = 1.0f;
        float linearAttenuation = 0.14f;
        float quadraticAttenuation = 0.07f;
    };

    struct SceneLighting
    {
        glm::vec3 ambientColor{1.0f};
        float ambientIntensity = 0.1f;
        DirectionalLight directionalLight;
        PointLight pointLight;
    };
}