#pragma once

#include "Scene/Transform.h"

#include <memory>
#include <vector>

namespace Kosmos
{
    class Mesh;
    class Material;
    class Model;

    struct RenderObject
    {
        std::shared_ptr<Mesh> mesh;
        std::shared_ptr<Material> material;
        Transform transform;
    };

    class Scene
    {
        public:
            void AddRenderObject(std::shared_ptr<Mesh> mesh, std::shared_ptr<Material> material, const Transform& transform);
            void AddModel(const Model& model, const Transform& transform);

            const std::vector<RenderObject>& GetRenderObjects() const { return m_RenderObjects; }

        private:
            std::vector<RenderObject> m_RenderObjects;
    };
}