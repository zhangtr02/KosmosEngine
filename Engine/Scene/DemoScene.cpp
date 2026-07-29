#include "Scene/DemoScene.h"
#include "Scene/Scene.h"
#include "Scene/Light.h"
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
#include <cmath>

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

            return std::make_shared<Texture>(width, height, std::move(pixels), TextureColorSpace::SRGB);
        }

        std::shared_ptr<Texture> CreateSolidTexture(const std::array<uint8_t, 4>& color, TextureColorSpace colorSpace)
        {
            return std::make_shared<Texture>(1, 1, std::vector<uint8_t>{color[0], color[1], color[2], color[3]}, colorSpace);
        }

        std::shared_ptr<Texture> CreateWaveNormalTexture()
        {
            constexpr uint32_t width = 128;
            constexpr uint32_t height = 128;
            constexpr float frequency = 2.0f;
            constexpr float strength = 0.60f;
            constexpr float twoPi = 6.28318530718f;
            std::vector<uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);

            for (uint32_t y = 0; y < height; ++y)
            {
                for (uint32_t x = 0; x < width; ++x)
                {
                    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
                    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
                    const float normalX = std::sin(u * frequency * twoPi) * strength;
                    const float normalY = std::sin(v * frequency * twoPi) * strength;
                    const float normalZ = std::sqrt(1.0f - normalX * normalX - normalY * normalY);
                    const size_t pixelIndex = (static_cast<size_t>(y) * width + x) * 4;

                    pixels[pixelIndex] = static_cast<uint8_t>((normalX * 0.5f + 0.5f) * 255.0f + 0.5f);
                    pixels[pixelIndex + 1] = static_cast<uint8_t>((normalY * 0.5f + 0.5f) * 255.0f + 0.5f);
                    pixels[pixelIndex + 2] = static_cast<uint8_t>((normalZ * 0.5f + 0.5f) * 255.0f + 0.5f);
                    pixels[pixelIndex + 3] = 255;
                }
            }

            return std::make_shared<Texture>(width, height, std::move(pixels), TextureColorSpace::Linear);
        }
    }

    std::unique_ptr<Scene> CreateDemoScene()
    {
        auto scene = std::make_unique<Scene>();

        const std::shared_ptr<Texture> groundTexture = CreateCheckerboardTexture({172, 180, 192, 255}, {76, 84, 98, 255});
        const std::shared_ptr<Texture> whiteTexture = CreateSolidTexture({255, 255, 255, 255}, TextureColorSpace::SRGB);

        const std::shared_ptr<Texture> groundOrmTexture = CreateSolidTexture({255, 224, 0, 255}, TextureColorSpace::Linear);
        const std::shared_ptr<Texture> stoneOrmTexture = CreateSolidTexture({255, 90, 0, 255}, TextureColorSpace::Linear);
        const std::shared_ptr<Texture> orbOrmTexture = CreateSolidTexture({255, 46, 209, 255}, TextureColorSpace::Linear);
        const std::shared_ptr<Texture> flatNormalTexture = CreateSolidTexture({128, 128, 255, 255}, TextureColorSpace::Linear);
        const std::shared_ptr<Texture> waveNormalTexture = CreateWaveNormalTexture();

        const std::shared_ptr<Material> groundMaterial = std::make_shared<Material>(glm::vec4(0.72f, 0.76f, 0.82f, 1.0f), groundTexture, groundOrmTexture, waveNormalTexture, 1.0f, 1.0f, 1.0f, 0.0f);
        const std::shared_ptr<Material> stoneMaterial = std::make_shared<Material>(glm::vec4(0.46f, 0.54f, 0.70f, 1.0f), whiteTexture, stoneOrmTexture, waveNormalTexture, 1.0f, 1.0f, 1.0f, 0.0f);
        const std::shared_ptr<Material> orbMaterial = std::make_shared<Material>(glm::vec4(0.10f, 0.48f, 0.92f, 1.0f), whiteTexture, orbOrmTexture, flatNormalTexture, 1.0f, 1.0f, 1.0f, 0.0f);

        const ObjLoader::MaterialMap materials = {
            {"Ground", groundMaterial},
            {"Stone", stoneMaterial},
            {"Orb", orbMaterial}
        };

        const std::filesystem::path modelDirectory = std::filesystem::path(KOSMOS_ASSET_DIR) / "Models";
        const Model ground = ObjLoader::Load(modelDirectory / "CourtyardGround.obj", materials, groundMaterial);
        const Model pillar = ObjLoader::Load(modelDirectory / "SteppedPillar.obj", materials, stoneMaterial);
        const Model orb = ObjLoader::Load(modelDirectory / "Icosphere.obj", materials, orbMaterial);

        scene->AddModel(ground, Transform{
            glm::vec3(0.0f, -1.0f, 0.0f)
        });

        scene->AddModel(pillar, Transform{
            glm::vec3(-1.8f, -1.0f, 0.0f),
            glm::vec3(0.0f, glm::radians(18.0f), 0.0f),
            glm::vec3(1.0f)
        });

        scene->AddModel(orb, Transform{
            glm::vec3(-1.8f, 2.6f, 0.0f),
            glm::vec3(0.0f),
            glm::vec3(0.42f)
        });

        scene->AddModel(pillar, Transform{
            glm::vec3(0.25f, -1.0f, -0.55f),
            glm::vec3(glm::radians(-4.0f), glm::radians(-28.0f), glm::radians(-5.0f)),
            glm::vec3(0.82f, 1.12f, 0.82f)
        });

        scene->AddModel(orb, Transform{
            glm::vec3(2.0f, 0.05f, 0.55f),
            glm::vec3(0.0f, glm::radians(20.0f), 0.0f),
            glm::vec3(0.55f)
        });

        scene->AddModel(pillar, Transform{
            glm::vec3(1.55f, -1.0f, -2.0f),
            glm::vec3(0.0f, glm::radians(42.0f), 0.0f),
            glm::vec3(0.62f)
        });

        SceneLighting lighting{};
        lighting.ambientColor = glm::vec3(0.16f, 0.20f, 0.28f);
        lighting.ambientIntensity = 0.12f;

        lighting.directionalLight.direction = glm::vec3(-0.58f, -1.0f, -0.36f);
        lighting.directionalLight.color = glm::vec3(1.0f, 0.86f, 0.68f);
        lighting.directionalLight.intensity = 2.5f;
        lighting.directionalLight.shadowCenter = glm::vec3(0.0f, 0.5f, 0.0f);
        lighting.directionalLight.shadowHalfExtent = 7.0f;
        lighting.directionalLight.shadowDistance = 12.0f;
        lighting.directionalLight.shadowNearPlane = 0.1f;
        lighting.directionalLight.shadowFarPlane = 30.0f;
        lighting.directionalLight.shadowMapResolution = 2048;
        lighting.directionalLight.shadowDepthBiasConstant = 1.25f;
        lighting.directionalLight.shadowDepthBiasSlope = 1.75f;
        lighting.directionalLight.shadowReceiverBias = 0.0005f;
        lighting.directionalLight.shadowNormalBias = 0.0025f;
        lighting.directionalLight.shadowStrength = 0.95f;
        lighting.directionalLight.shadowFilterRadius = 1.5f;

        lighting.pointLight.position = glm::vec3(2.8f, 3.0f, 3.2f);
        lighting.pointLight.color = glm::vec3(0.28f, 0.62f, 1.0f);
        lighting.pointLight.intensity = 0.6f;
        lighting.pointLight.constantAttenuation = 1.0f;
        lighting.pointLight.linearAttenuation = 0.22f;
        lighting.pointLight.quadraticAttenuation = 0.20f;

        scene->SetLighting(lighting);
        return scene;
    }
}
