#include "Scene/Scene.h"
#include "Renderer/Mesh.h"
#include "Renderer/Material.h"

#include <stdexcept>
#include <utility>

namespace Kosmos
{
    void Scene::AddRenderObject(std::shared_ptr<Mesh> mesh, std::shared_ptr<Material> material, const Transform& transform)
    {
        if (!mesh)
        {
            throw std::runtime_error("Cannot add a render object without a mesh!");
        }

        if (!material)
        {
            throw std::runtime_error("Cannot add a render object without a material!");
        }

        m_RenderObjects.push_back({std::move(mesh), std::move(material), transform});
    }
}