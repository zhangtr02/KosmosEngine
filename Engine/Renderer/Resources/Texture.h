#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Kosmos
{
    enum class TextureColorSpace { Linear, SRGB };
    enum class TextureDataType { UInt8, Float32 };

    class Texture
    {
        public:
            Texture(uint32_t width, uint32_t height, std::vector<uint8_t> pixels, TextureColorSpace colorSpace);
            Texture(uint32_t width, uint32_t height, std::vector<float> pixels);

            uint32_t GetWidth() const { return m_Width; }
            uint32_t GetHeight() const { return m_Height; }
            TextureColorSpace GetColorSpace() const { return m_ColorSpace; }
            TextureDataType GetDataType() const { return m_DataType; }
            const std::vector<uint8_t>& GetBytePixels() const { return m_BytePixels; }
            const std::vector<float>& GetFloatPixels() const { return m_FloatPixels; }
            const void* GetData() const;
            size_t GetByteSize() const;

        private:
            uint32_t m_Width = 0;
            uint32_t m_Height = 0;
            TextureColorSpace m_ColorSpace = TextureColorSpace::Linear;
            TextureDataType m_DataType = TextureDataType::UInt8;
            std::vector<uint8_t> m_BytePixels;
            std::vector<float> m_FloatPixels;
    };
}