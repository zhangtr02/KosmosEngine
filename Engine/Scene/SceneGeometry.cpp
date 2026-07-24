#include "Scene/SceneGeometry.h"

namespace Kosmos
{
    namespace
    {
        void AddTriangle(SceneGeometry& geometry, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& color)
        {
            const uint16_t baseIndex = static_cast<uint16_t>(geometry.vertices.size());

            geometry.vertices.push_back({a, color});
            geometry.vertices.push_back({b, color});
            geometry.vertices.push_back({c, color});

            geometry.indices.insert(geometry.indices.end(), {
                baseIndex,
                static_cast<uint16_t>(baseIndex + 1),
                static_cast<uint16_t>(baseIndex + 2)
            });
        }

        void AddQuad(SceneGeometry& geometry, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d, const glm::vec3& color)
        {
            const uint16_t baseIndex = static_cast<uint16_t>(geometry.vertices.size());

            geometry.vertices.push_back({a, color});
            geometry.vertices.push_back({b, color});
            geometry.vertices.push_back({c, color});
            geometry.vertices.push_back({d, color});

            geometry.indices.insert(geometry.indices.end(), {
                baseIndex,
                static_cast<uint16_t>(baseIndex + 1),
                static_cast<uint16_t>(baseIndex + 2),
                static_cast<uint16_t>(baseIndex + 2),
                static_cast<uint16_t>(baseIndex + 3),
                baseIndex
            });
        }

        void AddBox(SceneGeometry& geometry, const glm::vec3& center, const glm::vec3& halfExtent, const glm::vec3& color)
        {
            const float left = center.x - halfExtent.x;
            const float right = center.x + halfExtent.x;
            const float bottom = center.y - halfExtent.y;
            const float top = center.y + halfExtent.y;
            const float back = center.z - halfExtent.z;
            const float front = center.z + halfExtent.z;

            AddQuad(geometry, {left, bottom, front}, {right, bottom, front}, {right, top, front}, {left, top, front}, color);
            AddQuad(geometry, {right, bottom, back}, {left, bottom, back}, {left, top, back}, {right, top, back}, color * 0.55f);
            AddQuad(geometry, {left, bottom, back}, {left, bottom, front}, {left, top, front}, {left, top, back}, color * 0.65f);
            AddQuad(geometry, {right, bottom, front}, {right, bottom, back}, {right, top, back}, {right, top, front}, color * 0.85f);
            AddQuad(geometry, {left, top, front}, {right, top, front}, {right, top, back}, {left, top, back}, color * 1.15f);
            AddQuad(geometry, {left, bottom, back}, {right, bottom, back}, {right, bottom, front}, {left, bottom, front}, color * 0.35f);
        }

        void AddPyramid(SceneGeometry& geometry, const glm::vec3& baseCenter, const glm::vec2& halfExtent, float height, const glm::vec3& color)
        {
            const glm::vec3 frontLeft{baseCenter.x - halfExtent.x, baseCenter.y, baseCenter.z + halfExtent.y};
            const glm::vec3 frontRight{baseCenter.x + halfExtent.x, baseCenter.y, baseCenter.z + halfExtent.y};
            const glm::vec3 backRight{baseCenter.x + halfExtent.x, baseCenter.y, baseCenter.z - halfExtent.y};
            const glm::vec3 backLeft{baseCenter.x - halfExtent.x, baseCenter.y, baseCenter.z - halfExtent.y};
            const glm::vec3 apex{baseCenter.x, baseCenter.y + height, baseCenter.z};

            AddTriangle(geometry, frontLeft, frontRight, apex, color);
            AddTriangle(geometry, frontRight, backRight, apex, color * 0.82f);
            AddTriangle(geometry, backRight, backLeft, apex, color * 0.55f);
            AddTriangle(geometry, backLeft, frontLeft, apex, color * 0.68f);
            AddQuad(geometry, backLeft, backRight, frontRight, frontLeft, color * 0.35f);
        }

        void AddCrystal(SceneGeometry& geometry, const glm::vec3& center, const glm::vec3& radius, const glm::vec3& colorA, const glm::vec3& colorB)
        {
            const glm::vec3 top{center.x, center.y + radius.y, center.z};
            const glm::vec3 bottom{center.x, center.y - radius.y, center.z};
            const glm::vec3 front{center.x, center.y, center.z + radius.z};
            const glm::vec3 right{center.x + radius.x, center.y, center.z};
            const glm::vec3 back{center.x, center.y, center.z - radius.z};
            const glm::vec3 left{center.x - radius.x, center.y, center.z};

            AddTriangle(geometry, front, right, top, colorA);
            AddTriangle(geometry, right, back, top, colorB);
            AddTriangle(geometry, back, left, top, colorA * 0.75f);
            AddTriangle(geometry, left, front, top, colorB * 0.85f);

            AddTriangle(geometry, right, front, bottom, colorA * 0.65f);
            AddTriangle(geometry, back, right, bottom, colorB * 0.65f);
            AddTriangle(geometry, left, back, bottom, colorA * 0.50f);
            AddTriangle(geometry, front, left, bottom, colorB * 0.55f);
        }
    }

    DemoScene CreateDemoScene()
    {
        DemoScene scene;
        SceneGeometry& geometry = scene.geometry;

        geometry.vertices.reserve(384);
        geometry.indices.reserve(600);

        AddBox(geometry, {0.0f, -0.78f, 0.0f}, {2.4f, 0.08f, 2.1f}, {0.10f, 0.16f, 0.22f});

        AddBox(geometry, {0.0f, -0.58f, 0.0f}, {1.55f, 0.12f, 1.10f}, {0.24f, 0.17f, 0.38f});
        AddBox(geometry, {0.0f, -0.36f, 0.0f}, {1.25f, 0.10f, 0.88f}, {0.35f, 0.22f, 0.50f});
        AddBox(geometry, {0.0f, -0.18f, 0.0f}, {1.02f, 0.08f, 0.70f}, {0.46f, 0.28f, 0.60f});

        AddBox(geometry, {-0.82f, 0.48f, 0.03f}, {0.15f, 0.58f, 0.15f}, {0.68f, 0.37f, 0.18f});
        AddBox(geometry, {0.82f, 0.48f, 0.03f}, {0.15f, 0.58f, 0.15f}, {0.68f, 0.37f, 0.18f});

        AddBox(geometry, {-0.82f, 1.12f, 0.03f}, {0.23f, 0.12f, 0.23f}, {0.78f, 0.48f, 0.24f});
        AddBox(geometry, {0.82f, 1.12f, 0.03f}, {0.23f, 0.12f, 0.23f}, {0.78f, 0.48f, 0.24f});

        AddBox(geometry, {0.0f, 1.34f, 0.03f}, {1.18f, 0.13f, 0.28f}, {0.46f, 0.25f, 0.58f});
        AddPyramid(geometry, {0.0f, 1.47f, 0.03f}, {1.42f, 0.62f}, 0.58f, {0.58f, 0.24f, 0.62f});

        AddCrystal(geometry, {0.0f, 0.43f, 0.10f}, {0.34f, 0.52f, 0.34f}, {0.12f, 0.82f, 0.92f}, {0.55f, 0.24f, 0.90f});
        AddCrystal(geometry, {-1.62f, -0.38f, 0.42f}, {0.19f, 0.31f, 0.19f}, {0.10f, 0.66f, 0.78f}, {0.30f, 0.22f, 0.76f});
        AddCrystal(geometry, {1.55f, -0.40f, -0.52f}, {0.17f, 0.29f, 0.17f}, {0.76f, 0.20f, 0.58f}, {0.36f, 0.20f, 0.78f});

        AddBox(geometry, {-1.78f, -0.50f, -0.72f}, {0.24f, 0.20f, 0.28f}, {0.18f, 0.34f, 0.38f});
        AddBox(geometry, {1.82f, -0.54f, 0.78f}, {0.30f, 0.16f, 0.22f}, {0.24f, 0.30f, 0.42f});

        scene.objectTransforms = {
            Transform{
                glm::vec3(0.0f, 0.0f, 0.0f),
                glm::vec3(0.0f, 0.0f, 0.0f),
                glm::vec3(0.72f)
            },
            Transform{
                glm::vec3(-2.15f, -0.42f, -0.55f),
                glm::vec3(0.0f, glm::radians(28.0f), 0.0f),
                glm::vec3(0.25f)
            },
            Transform{
                glm::vec3(2.0f, -0.40f, 0.45f),
                glm::vec3(0.0f, glm::radians(-35.0f), 0.0f),
                glm::vec3(0.28f)
            }
        };

        return scene;
    }
}