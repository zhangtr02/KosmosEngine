#pragma once

#include "Renderer/Texture.h"

#include <filesystem>
#include <memory>

namespace Kosmos
{
    class TextureLoader
    {
        public:
            static std::shared_ptr<Texture> Load(const std::filesystem::path& path, TextureColorSpace colorSpace);
    };
}