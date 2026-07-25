#include "Scene/Scene.h"
#include "Renderer/Mesh.h"
#include "Renderer/Texture.h"

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

    void Scene::AddTexture(std::shared_ptr<Texture> texture)
    {
        if (!texture)
        {
            throw std::runtime_error("Cannot add a null texture to the scene!");
        }

        m_Textures.push_back(std::move(texture));
    }
}