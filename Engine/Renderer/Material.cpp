#include "Renderer/Material.h"
#include "Renderer/Texture.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace Kosmos
{
    Material::Material(const glm::vec4& baseColor, std::shared_ptr<Texture> baseColorTexture, std::shared_ptr<Texture> ormTexture, std::shared_ptr<Texture> normalTexture, float metallic, float roughness, float ambientOcclusion, float emissiveStrength)
        : m_BaseColor(baseColor), m_BaseColorTexture(std::move(baseColorTexture)), m_OrmTexture(std::move(ormTexture)), m_NormalTexture(std::move(normalTexture)), m_Metallic(metallic), m_Roughness(roughness), m_AmbientOcclusion(ambientOcclusion), m_EmissiveStrength(emissiveStrength)
    {
        if (!m_BaseColorTexture)
        {
            throw std::runtime_error("PBR material requires a base color texture!");
        }

        if (!m_OrmTexture)
        {
            throw std::runtime_error("PBR material requires an ORM texture!");
        }

        if (!m_NormalTexture)
        {
            throw std::runtime_error("PBR material requires a normal texture!");
        }

        if (m_BaseColorTexture->GetColorSpace() != TextureColorSpace::SRGB)
        {
            throw std::runtime_error("Material base color texture must use the sRGB color space!");
        }

        if (m_OrmTexture->GetColorSpace() != TextureColorSpace::Linear)
        {
            throw std::runtime_error("Material ORM texture must use the linear color space!");
        }

        if (m_NormalTexture->GetColorSpace() != TextureColorSpace::Linear)
        {
            throw std::runtime_error("Material normal texture must use the linear color space!");
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