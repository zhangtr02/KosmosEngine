#include "Renderer/Material.h"
#include "Renderer/Texture.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace Kosmos
{
    Material::Material(const glm::vec4& baseColor, std::shared_ptr<Texture> baseColorTexture, float metallic, float roughness, float ambientOcclusion, float emissiveStrength)
        : m_BaseColor(baseColor), m_BaseColorTexture(std::move(baseColorTexture)), m_Metallic(metallic), m_Roughness(roughness), m_AmbientOcclusion(ambientOcclusion), m_EmissiveStrength(emissiveStrength)
    {
        if (!m_BaseColorTexture)
        {
            throw std::runtime_error("PBR material requires a base color texture!");
        }

        if (!std::isfinite(m_Metallic) || m_Metallic < 0.0f || m_Metallic > 1.0f)
        {
            throw std::runtime_error("Material metallic must be between zero and one!");
        }

        if (!std::isfinite(m_Roughness) || m_Roughness < 0.0f || m_Roughness > 1.0f)
        {
            throw std::runtime_error("Material roughness must be between zero and one!");
        }

        if (!std::isfinite(m_AmbientOcclusion) || m_AmbientOcclusion < 0.0f || m_AmbientOcclusion > 1.0f)
        {
            throw std::runtime_error("Material ambient occlusion must be between zero and one!");
        }

        if (!std::isfinite(m_EmissiveStrength) || m_EmissiveStrength < 0.0f)
        {
            throw std::runtime_error("Material emissive strength cannot be negative!");
        }
    }
}