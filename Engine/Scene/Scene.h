#pragma once

#include "Scene/Transform.h"

#include <memory>
#include <vector>

namespace Kosmos
{
    class Mesh;

    struct RenderObject
    {
        std::shared_ptr<Mesh> mesh;
        Transform transform;
    };

    class Scene
    {
        public:
            void AddRenderObject(std::shared_ptr<Mesh> mesh, const Transform& transform);

            const std::vector<RenderObject>& GetRenderObjects() const { return m_RenderObjects; }

        private:
            std::vector<RenderObject> m_RenderObjects;
    };
}