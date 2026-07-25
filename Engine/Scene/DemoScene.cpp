#include "Scene/DemoScene.h"
#include "Scene/Scene.h"
#include "Renderer/Mesh.h"
#include "Renderer/Texture.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace Kosmos
{
    namespace
    {
        struct MeshData
        {
            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;
        };

        void AddTriangle(MeshData& mesh, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& color)
        {
            const uint32_t baseIndex = static_cast<uint32_t>(mesh.vertices.size());

            mesh.vertices.push_back({a, color, {0.0f, 1.0f}});
            mesh.vertices.push_back({b, color, {1.0f, 1.0f}});
            mesh.vertices.push_back({c, color, {0.5f, 0.0f}});

            mesh.indices.insert(mesh.indices.end(), {
                baseIndex,
                baseIndex + 1,
                baseIndex + 2
            });
        }

        void AddQuad(MeshData& mesh, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d, const glm::vec3& color)
        {
            const uint32_t baseIndex = static_cast<uint32_t>(mesh.vertices.size());

            mesh.vertices.push_back({a, color, {0.0f, 1.0f}});
            mesh.vertices.push_back({b, color, {1.0f, 1.0f}});
            mesh.vertices.push_back({c, color, {1.0f, 0.0f}});
            mesh.vertices.push_back({d, color, {0.0f, 0.0f}});

            mesh.indices.insert(mesh.indices.end(), {
                baseIndex,
                baseIndex + 1,
                baseIndex + 2,
                baseIndex + 2,
                baseIndex + 3,
                baseIndex
            });
        }

        void AddBox(MeshData& mesh, const glm::vec3& center, const glm::vec3& halfExtent, const glm::vec3& color)
        {
            const float left = center.x - halfExtent.x;
            const float right = center.x + halfExtent.x;
            const float bottom = center.y - halfExtent.y;
            const float top = center.y + halfExtent.y;
            const float back = center.z - halfExtent.z;
            const float front = center.z + halfExtent.z;

            AddQuad(mesh, {left, bottom, front}, {right, bottom, front}, {right, top, front}, {left, top, front}, color);
            AddQuad(mesh, {right, bottom, back}, {left, bottom, back}, {left, top, back}, {right, top, back}, color * 0.55f);
            AddQuad(mesh, {left, bottom, back}, {left, bottom, front}, {left, top, front}, {left, top, back}, color * 0.65f);
            AddQuad(mesh, {right, bottom, front}, {right, bottom, back}, {right, top, back}, {right, top, front}, color * 0.85f);
            AddQuad(mesh, {left, top, front}, {right, top, front}, {right, top, back}, {left, top, back}, color * 1.15f);
            AddQuad(mesh, {left, bottom, back}, {right, bottom, back}, {right, bottom, front}, {left, bottom, front}, color * 0.35f);
        }

        void AddPyramid(MeshData& mesh, const glm::vec3& baseCenter, const glm::vec2& halfExtent, float height, const glm::vec3& color)
        {
            const glm::vec3 frontLeft{baseCenter.x - halfExtent.x, baseCenter.y, baseCenter.z + halfExtent.y};
            const glm::vec3 frontRight{baseCenter.x + halfExtent.x, baseCenter.y, baseCenter.z + halfExtent.y};
            const glm::vec3 backRight{baseCenter.x + halfExtent.x, baseCenter.y, baseCenter.z - halfExtent.y};
            const glm::vec3 backLeft{baseCenter.x - halfExtent.x, baseCenter.y, baseCenter.z - halfExtent.y};
            const glm::vec3 apex{baseCenter.x, baseCenter.y + height, baseCenter.z};

            AddTriangle(mesh, frontLeft, frontRight, apex, color);
            AddTriangle(mesh, frontRight, backRight, apex, color * 0.82f);
            AddTriangle(mesh, backRight, backLeft, apex, color * 0.55f);
            AddTriangle(mesh, backLeft, frontLeft, apex, color * 0.68f);
            AddQuad(mesh, backLeft, backRight, frontRight, frontLeft, color * 0.35f);
        }

        std::shared_ptr<Mesh> CreateTempleMesh()
        {
            MeshData mesh;
            mesh.vertices.reserve(320);
            mesh.indices.reserve(480);

            AddBox(mesh, {0.0f, -0.78f, 0.0f}, {2.4f, 0.08f, 2.1f}, {0.10f, 0.16f, 0.22f});

            AddBox(mesh, {0.0f, -0.58f, 0.0f}, {1.55f, 0.12f, 1.10f}, {0.24f, 0.17f, 0.38f});
            AddBox(mesh, {0.0f, -0.36f, 0.0f}, {1.25f, 0.10f, 0.88f}, {0.35f, 0.22f, 0.50f});
            AddBox(mesh, {0.0f, -0.18f, 0.0f}, {1.02f, 0.08f, 0.70f}, {0.46f, 0.28f, 0.60f});

            AddBox(mesh, {-0.82f, 0.48f, 0.03f}, {0.15f, 0.58f, 0.15f}, {0.68f, 0.37f, 0.18f});
            AddBox(mesh, {0.82f, 0.48f, 0.03f}, {0.15f, 0.58f, 0.15f}, {0.68f, 0.37f, 0.18f});

            AddBox(mesh, {-0.82f, 1.12f, 0.03f}, {0.23f, 0.12f, 0.23f}, {0.78f, 0.48f, 0.24f});
            AddBox(mesh, {0.82f, 1.12f, 0.03f}, {0.23f, 0.12f, 0.23f}, {0.78f, 0.48f, 0.24f});

            AddBox(mesh, {0.0f, 1.34f, 0.03f}, {1.18f, 0.13f, 0.28f}, {0.46f, 0.25f, 0.58f});
            AddPyramid(mesh, {0.0f, 1.47f, 0.03f}, {1.42f, 0.62f}, 0.58f, {0.58f, 0.24f, 0.62f});

            AddBox(mesh, {-1.78f, -0.50f, -0.72f}, {0.24f, 0.20f, 0.28f}, {0.18f, 0.34f, 0.38f});
            AddBox(mesh, {1.82f, -0.54f, 0.78f}, {0.30f, 0.16f, 0.22f}, {0.24f, 0.30f, 0.42f});

            return std::make_shared<Mesh>(std::move(mesh.vertices), std::move(mesh.indices));
        }

        std::shared_ptr<Mesh> CreateCrystalMesh()
        {
            MeshData mesh;
            mesh.vertices.reserve(24);
            mesh.indices.reserve(24);

            const glm::vec3 top{0.0f, 1.0f, 0.0f};
            const glm::vec3 bottom{0.0f, -1.0f, 0.0f};
            const glm::vec3 front{0.0f, 0.0f, 1.0f};
            const glm::vec3 right{1.0f, 0.0f, 0.0f};
            const glm::vec3 back{0.0f, 0.0f, -1.0f};
            const glm::vec3 left{-1.0f, 0.0f, 0.0f};

            const glm::vec3 cyan{0.12f, 0.82f, 0.92f};
            const glm::vec3 purple{0.55f, 0.24f, 0.90f};

            AddTriangle(mesh, front, right, top, cyan);
            AddTriangle(mesh, right, back, top, purple);
            AddTriangle(mesh, back, left, top, cyan * 0.75f);
            AddTriangle(mesh, left, front, top, purple * 0.85f);

            AddTriangle(mesh, right, front, bottom, cyan * 0.65f);
            AddTriangle(mesh, back, right, bottom, purple * 0.65f);
            AddTriangle(mesh, left, back, bottom, cyan * 0.50f);
            AddTriangle(mesh, front, left, bottom, purple * 0.55f);

            return std::make_shared<Mesh>(std::move(mesh.vertices), std::move(mesh.indices));
        }

        std::shared_ptr<Texture> CreateCheckerboardTexture()
        {
            constexpr uint32_t width = 128;
            constexpr uint32_t height = 128;
            constexpr uint32_t tileSize = 16;

            std::vector<uint8_t> pixels(
                static_cast<size_t>(width) * static_cast<size_t>(height) * 4);

            for (uint32_t y = 0; y < height; ++y)
            {
                for (uint32_t x = 0; x < width; ++x)
                {
                    const bool firstColor = ((x / tileSize) + (y / tileSize)) % 2 == 0;
                    const size_t pixelIndex = (static_cast<size_t>(y) * width + x) * 4;

                    pixels[pixelIndex + 0] = firstColor ? 34 : 160;
                    pixels[pixelIndex + 1] = firstColor ? 196 : 48;
                    pixels[pixelIndex + 2] = firstColor ? 220 : 196;
                    pixels[pixelIndex + 3] = 255;
                }
            }

            return std::make_shared<Texture>(
                width,
                height,
                std::move(pixels));
        }
    }

    std::unique_ptr<Scene> CreateDemoScene()
    {
        auto scene = std::make_unique<Scene>();

        const std::shared_ptr<Mesh> templeMesh = CreateTempleMesh();
        const std::shared_ptr<Mesh> crystalMesh = CreateCrystalMesh();

        scene->AddRenderObject(
            templeMesh,
            Transform{
                glm::vec3(0.0f),
                glm::vec3(0.0f),
                glm::vec3(0.72f)
            });

        scene->AddRenderObject(
            templeMesh,
            Transform{
                glm::vec3(-2.15f, -0.42f, -0.55f),
                glm::vec3(0.0f, glm::radians(28.0f), 0.0f),
                glm::vec3(0.25f)
            });

        scene->AddRenderObject(
            templeMesh,
            Transform{
                glm::vec3(2.0f, -0.40f, 0.45f),
                glm::vec3(0.0f, glm::radians(-35.0f), 0.0f),
                glm::vec3(0.28f)
            });

        scene->AddRenderObject(
            crystalMesh,
            Transform{
                glm::vec3(0.0f, 0.40f, 0.08f),
                glm::vec3(0.0f),
                glm::vec3(0.34f, 0.45f, 0.34f)
            });

        scene->AddRenderObject(
            crystalMesh,
            Transform{
                glm::vec3(-1.65f, -0.35f, 0.42f),
                glm::vec3(0.0f, glm::radians(20.0f), 0.0f),
                glm::vec3(0.19f, 0.27f, 0.19f)
            });

        scene->AddRenderObject(
            crystalMesh,
            Transform{
                glm::vec3(1.55f, -0.36f, -0.52f),
                glm::vec3(0.0f, glm::radians(-25.0f), 0.0f),
                glm::vec3(0.17f, 0.25f, 0.17f)
            });

        scene->AddTexture(CreateCheckerboardTexture());

        return scene;
    }
}