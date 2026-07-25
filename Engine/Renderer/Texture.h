#pragma once

#include <cstdint>
#include <vector>

namespace Kosmos
{
    class Texture
    {
        public:
            Texture(uint32_t width, uint32_t height, std::vector<uint8_t> pixels);

            uint32_t GetWidth() const { return m_Width; }
            uint32_t GetHeight() const { return m_Height; }
            const std::vector<uint8_t>& GetPixels() const { return m_Pixels; }

        private:
            uint32_t m_Width = 0;
            uint32_t m_Height = 0;
            std::vector<uint8_t> m_Pixels;
    };
}