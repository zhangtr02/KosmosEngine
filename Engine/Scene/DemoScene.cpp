#include "Scene/DemoScene.h"
#include "Scene/Scene.h"
#include "Scene/Light.h"
#include "Renderer/Material.h"
#include "Renderer/Model.h"
#include "Renderer/ObjLoader.h"
#include "Renderer/TextureLoader.h"

#include <glm/glm.hpp>
#include <filesystem>
#include <memory>
#include <array>

namespace Kosmos
{
    std::unique_ptr<Scene> CreateDemoScene()
    {
        auto scene = std::make_unique<Scene>();

        const std::filesystem::path textureDirectory = std::filesystem::path(KOSMOS_ASSET_DIR) / "Textures";

        const std::filesystem::path skyboxDirectory = textureDirectory / "Skybox";

        const std::array<std::filesystem::path, CubeTexture::FaceCount> skyboxPaths = {
            skyboxDirectory / "PositiveX.png",
            skyboxDirectory / "NegativeX.png",
            skyboxDirectory / "PositiveY.png",
            skyboxDirectory / "NegativeY.png",
            skyboxDirectory / "PositiveZ.png",
            skyboxDirectory / "NegativeZ.png"
        };

        scene->SetEnvironment(TextureLoader::LoadCube(skyboxPaths, TextureColorSpace::SRGB));

        const std::shared_ptr<Texture> groundBaseColorTexture = TextureLoader::Load(textureDirectory / "Ground_BaseColor.png", TextureColorSpace::SRGB);
        const std::shared_ptr<Texture> groundOrmTexture = TextureLoader::Load(textureDirectory / "Ground_ORM.png", TextureColorSpace::Linear);
        const std::shared_ptr<Texture> groundNormalTexture = TextureLoader::Load(textureDirectory / "Ground_Normal.png", TextureColorSpace::Linear);
        const std::shared_ptr<Texture> stoneBaseColorTexture = TextureLoader::Load(textureDirectory / "Stone_BaseColor.png", TextureColorSpace::SRGB);
        const std::shared_ptr<Texture> stoneOrmTexture = TextureLoader::Load(textureDirectory / "Stone_ORM.png", TextureColorSpace::Linear);
        const std::shared_ptr<Texture> stoneNormalTexture = TextureLoader::Load(textureDirectory / "Stone_Normal.png", TextureColorSpace::Linear);
        const std::shared_ptr<Texture> orbBaseColorTexture = TextureLoader::Load(textureDirectory / "Orb_BaseColor.png", TextureColorSpace::SRGB);
        const std::shared_ptr<Texture> orbOrmTexture = TextureLoader::Load(textureDirectory / "Orb_ORM.png", TextureColorSpace::Linear);
        const std::shared_ptr<Texture> orbNormalTexture = TextureLoader::Load(textureDirectory / "Orb_Normal.png", TextureColorSpace::Linear);

        const std::shared_ptr<Material> groundMaterial = std::make_shared<Material>(glm::vec4(1.0f), groundBaseColorTexture, groundOrmTexture, groundNormalTexture, 1.0f, 1.0f, 1.0f, 0.0f);
        const std::shared_ptr<Material> stoneMaterial = std::make_shared<Material>(glm::vec4(1.0f), stoneBaseColorTexture, stoneOrmTexture, stoneNormalTexture, 1.0f, 1.0f, 1.0f, 0.0f);
        const std::shared_ptr<Material> orbMaterial = std::make_shared<Material>(glm::vec4(1.0f), orbBaseColorTexture, orbOrmTexture, orbNormalTexture, 1.0f, 1.0f, 1.0f, 0.0f);

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
