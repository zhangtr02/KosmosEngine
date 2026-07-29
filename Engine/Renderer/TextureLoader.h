#pragma once

#include "Renderer/Texture.h"
#include "Renderer/CubeTexture.h"

#include <array>
#include <filesystem>
#include <memory>

namespace Kosmos
{
    class TextureLoader
    {
        public:
            static std::shared_ptr<Texture> Load(const std::filesystem::path& path, TextureColorSpace colorSpace);
            static std::shared_ptr<CubeTexture> LoadCube(const std::array<std::filesystem::path, CubeTexture::FaceCount>& paths, TextureColorSpace colorSpace);
    };
}