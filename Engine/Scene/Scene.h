#pragma once

#include "Scene/Transform.h"

#include <memory>
#include <vector>

namespace Kosmos
{
    class Mesh;
    class Texture;

    struct RenderObject
    {
        std::shared_ptr<Mesh> mesh;
        Transform transform;
    };

    class Scene
    {
        public:
            void AddRenderObject(std::shared_ptr<Mesh> mesh, const Transform& transform);
            void AddTexture(std::shared_ptr<Texture> texture);

            const std::vector<RenderObject>& GetRenderObjects() const { return m_RenderObjects; }
            const std::vector<std::shared_ptr<Texture>>& GetTextures() const { return m_Textures; }

        private:
            std::vector<RenderObject> m_RenderObjects;
            std::vector<std::shared_ptr<Texture>> m_Textures;
    };
}