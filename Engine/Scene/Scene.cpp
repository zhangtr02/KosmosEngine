#include "Scene/Scene.h"
#include "Renderer/Mesh.h"

#include <stdexcept>
#include <utility>

namespace Kosmos
{
    void Scene::AddRenderObject(std::shared_ptr<Mesh> mesh, const Transform& transform)
    {
        if (!mesh)
        {
            throw std::runtime_error("Cannot add a render object without a mesh!");
        }

        m_RenderObjects.push_back({
            std::move(mesh),
            transform
        });
    }
}