#include "Renderer/Material.h"
#include "Renderer/Texture.h"

#include <stdexcept>
#include <utility>

namespace Kosmos
{
    Material::Material(const glm::vec4& baseColor, std::shared_ptr<Texture> baseColorTexture)
        : m_BaseColor(baseColor), m_BaseColorTexture(std::move(baseColorTexture))
    {
        if (!m_BaseColorTexture)
        {
            throw std::runtime_error("Basic material requires a base color texture!");
        }
    }
}