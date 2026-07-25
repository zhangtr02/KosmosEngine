#pragma once

#include <glm/glm.hpp>
#include <memory>

namespace Kosmos
{
    class Texture;

    class Material
    {
        public:
            Material(const glm::vec4& baseColor, std::shared_ptr<Texture> baseColorTexture);

            const glm::vec4& GetBaseColor() const { return m_BaseColor; }
            const std::shared_ptr<Texture>& GetBaseColorTexture() const { return m_BaseColorTexture; }

        private:
            glm::vec4 m_BaseColor{1.0f};
            std::shared_ptr<Texture> m_BaseColorTexture;
    };
}