#include "Renderer/Texture.h"

#include <stdexcept>
#include <utility>

namespace
{
    size_t GetExpectedElementCount(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0) throw std::runtime_error("Cannot create a texture with zero extent!");
        return static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    }
}

namespace Kosmos
{
    Texture::Texture(uint32_t width, uint32_t height, std::vector<uint8_t> pixels, TextureColorSpace colorSpace)
        : m_Width(width), m_Height(height), m_ColorSpace(colorSpace), m_DataType(TextureDataType::UInt8), m_BytePixels(std::move(pixels))
    {
        if (m_BytePixels.size() != GetExpectedElementCount(width, height)) throw std::runtime_error("RGBA8 texture contains an invalid number of elements!");
    }

    Texture::Texture(uint32_t width, uint32_t height, std::vector<float> pixels)
        : m_Width(width), m_Height(height), m_ColorSpace(TextureColorSpace::Linear), m_DataType(TextureDataType::Float32), m_FloatPixels(std::move(pixels))
    {
        if (m_FloatPixels.size() != GetExpectedElementCount(width, height)) throw std::runtime_error("RGBA32F texture contains an invalid number of elements!");
    }

    const void* Texture::GetData() const
    {
        return m_DataType == TextureDataType::Float32 ? static_cast<const void*>(m_FloatPixels.data()) : static_cast<const void*>(m_BytePixels.data());
    }

    size_t Texture::GetByteSize() const
    {
        return m_DataType == TextureDataType::Float32 ? m_FloatPixels.size() * sizeof(float) : m_BytePixels.size();
    }
}