#include "Renderer/Texture.h"

#include <stdexcept>
#include <utility>

namespace Kosmos
{
    Texture::Texture(uint32_t width, uint32_t height, std::vector<uint8_t> pixels, TextureColorSpace colorSpace)
        : m_Width(width), m_Height(height), m_Pixels(std::move(pixels)), m_ColorSpace(colorSpace)
    {
        if (m_Width == 0 || m_Height == 0)
        {
            throw std::runtime_error("Cannot create a texture with zero extent!");
        }

        const size_t expectedSize = static_cast<size_t>(m_Width) * static_cast<size_t>(m_Height) * 4;

        if (m_Pixels.size() != expectedSize)
        {
            throw std::runtime_error("Texture pixel data must contain four RGBA channels per pixel!");
        }
    }
}