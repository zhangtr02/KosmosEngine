#pragma once

#include <glm/glm.hpp>

namespace Kosmos
{
    struct DirectionalLight
    {
        glm::vec3 direction{-0.5f, -1.0f, -0.3f};
        glm::vec3 color{1.0f};
        float intensity = 1.0f;
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