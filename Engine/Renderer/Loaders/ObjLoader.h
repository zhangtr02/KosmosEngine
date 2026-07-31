#pragma once

#include "Renderer/Resources/Model.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

namespace Kosmos
{
    class Material;

    class ObjLoader
    {
        public:
            using MaterialMap = std::unordered_map<std::string, std::shared_ptr<Material>>;

            static Model Load(const std::filesystem::path& path, const MaterialMap& materials, std::shared_ptr<Material> defaultMaterial);
    };
}