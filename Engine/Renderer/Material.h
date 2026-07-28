#pragma once

#include <glm/glm.hpp>
#include <memory>

namespace Kosmos
{
    class Texture;

    class Material
    {
        public:
            Material(const glm::vec4& baseColor, std::shared_ptr<Texture> baseColorTexture, float metallic, float roughness, float ambientOcclusion, float emissiveStrength);

            const glm::vec4& GetBaseColor() const { return m_BaseColor; }
            const std::shared_ptr<Texture>& GetBaseColorTexture() const { return m_BaseColorTexture; }
            float GetMetallic() const { return m_Metallic; }
            float GetRoughness() const { return m_Roughness; }
            float GetAmbientOcclusion() const { return m_AmbientOcclusion; }
            float GetEmissiveStrength() const { return m_EmissiveStrength; }

        private:
            glm::vec4 m_BaseColor{1.0f};
            std::shared_ptr<Texture> m_BaseColorTexture;
            float m_Metallic = 0.0f;
            float m_Roughness = 0.5f;
            float m_AmbientOcclusion = 1.0f;
            float m_EmissiveStrength = 0.0f;
    };
}