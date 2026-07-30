#include "Renderer/CubeTexture.h"

#include <stdexcept>
#include <utility>

namespace Kosmos
{
    CubeTexture::CubeTexture(Faces faces)
        : m_Faces(std::move(faces))
    {
        if (!m_Faces[0]) throw std::runtime_error("Cube texture contains a null face!");

        const uint32_t width = m_Faces[0]->GetWidth();
        const uint32_t height = m_Faces[0]->GetHeight();
        const TextureColorSpace colorSpace = m_Faces[0]->GetColorSpace();
        const TextureDataType dataType = m_Faces[0]->GetDataType();

        if (width != height) throw std::runtime_error("Cube texture faces must be square!");

        for (const std::shared_ptr<Texture>& face : m_Faces)
        {
            if (!face) throw std::runtime_error("Cube texture contains a null face!");
            if (face->GetWidth() != width || face->GetHeight() != height) throw std::runtime_error("Cube texture faces must have identical extents!");
            if (face->GetColorSpace() != colorSpace) throw std::runtime_error("Cube texture faces must use the same color space!");
            if (face->GetDataType() != dataType) throw std::runtime_error("Cube texture faces must use the same data type!");
        }
    }
}