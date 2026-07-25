#include "Scene/DemoScene.h"
#include "Scene/Scene.h"
#include "Renderer/Material.h"
#include "Renderer/Model.h"
#include "Renderer/ObjLoader.h"
#include "Renderer/Texture.h"

#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

namespace Kosmos
{
    namespace
    {
        std::shared_ptr<Texture> CreateCheckerboardTexture(const std::array<uint8_t, 4>& firstColor, const std::array<uint8_t, 4>& secondColor)
        {
            constexpr uint32_t width = 128;
            constexpr uint32_t height = 128;
            constexpr uint32_t tileSize = 16;
            std::vector<uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);

            for (uint32_t y = 0; y < height; ++y)
            {
                for (uint32_t x = 0; x < width; ++x)
                {
                    const bool useFirstColor = ((x / tileSize) + (y / tileSize)) % 2 == 0;
                    const std::array<uint8_t, 4>& color = useFirstColor ? firstColor : secondColor;
                    const size_t pixelIndex = (static_cast<size_t>(y) * width + x) * 4;

                    for (uint32_t channel = 0; channel < 4; ++channel)
                    {
                        pixels[pixelIndex + channel] = color[channel];
                    }
                }
            }

            return std::make_shared<Texture>(width, height, std::move(pixels));
        }
    }

    std::unique_ptr<Scene> CreateDemoScene()
    {
        auto scene = std::make_unique<Scene>();

        const std::shared_ptr<Texture> stoneTexture = CreateCheckerboardTexture({220, 156, 74, 255}, {112, 52, 148, 255});
        const std::shared_ptr<Texture> crystalTexture = CreateCheckerboardTexture({34, 196, 220, 255}, {160, 48, 196, 255});

        const std::shared_ptr<Material> stoneMaterial = std::make_shared<Material>(glm::vec4(1.0f, 0.90f, 0.78f, 1.0f), stoneTexture);
        const std::shared_ptr<Material> crystalMaterial = std::make_shared<Material>(glm::vec4(0.78f, 0.94f, 1.0f, 1.0f), crystalTexture);

        const ObjLoader::MaterialMap materials = {
            {"Stone", stoneMaterial},
            {"Crystal", crystalMaterial}
        };

        const std::filesystem::path modelPath = std::filesystem::path(KOSMOS_ASSET_DIR) / "Models" / "Shrine.obj";
        const Model shrine = ObjLoader::Load(modelPath, materials, stoneMaterial);

        scene->AddModel(shrine, Transform{glm::vec3(0.0f, -0.15f, 0.0f), glm::vec3(0.0f), glm::vec3(0.8f)});
        scene->AddModel(shrine, Transform{glm::vec3(-2.1f, -0.55f, -0.6f), glm::vec3(0.0f, glm::radians(25.0f), 0.0f), glm::vec3(0.30f)});
        scene->AddModel(shrine, Transform{glm::vec3(2.0f, -0.55f, 0.45f), glm::vec3(0.0f, glm::radians(-30.0f), 0.0f), glm::vec3(0.32f)});

        return scene;
    }
}