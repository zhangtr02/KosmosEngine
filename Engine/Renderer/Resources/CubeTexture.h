#pragma once

#include "Renderer/Resources/Texture.h"

#include <array>
#include <memory>

namespace Kosmos
{
    class CubeTexture
    {
        public:
            static constexpr uint32_t FaceCount = 6;
            using Faces = std::array<std::shared_ptr<Texture>, FaceCount>;

            explicit CubeTexture(Faces faces);

            uint32_t GetWidth() const { return m_Faces[0]->GetWidth(); }
            uint32_t GetHeight() const { return m_Faces[0]->GetHeight(); }
            TextureColorSpace GetColorSpace() const { return m_Faces[0]->GetColorSpace(); }
            const Faces& GetFaces() const { return m_Faces; }

        private:
            Faces m_Faces;
    };
}