#include "Scene/Scene.h"
#include "Renderer/Mesh.h"
#include "Renderer/Material.h"
#include "Renderer/Model.h"

#include <glm/geometric.hpp>
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

    void Scene::AddModel(const Model& model, const Transform& transform)
    {
        for (const ModelPart& part : model.GetParts())
        {
            AddRenderObject(part.mesh, part.material, transform);
        }
    }

    void Scene::SetLighting(const SceneLighting& lighting)
    {
        if (glm::dot(lighting.directionalLight.direction, lighting.directionalLight.direction) <= 0.0f)
        {
            throw std::runtime_error("Directional light direction cannot be zero!");
        }

        if (lighting.ambientIntensity < 0.0f || lighting.directionalLight.intensity < 0.0f || lighting.pointLight.intensity < 0.0f)
        {
            throw std::runtime_error("Light intensity cannot be negative!");
        }

        if (lighting.pointLight.constantAttenuation < 0.0f || lighting.pointLight.linearAttenuation < 0.0f || lighting.pointLight.quadraticAttenuation < 0.0f)
        {
            throw std::runtime_error("Point light attenuation cannot be negative!");
        }

        if (lighting.pointLight.constantAttenuation == 0.0f && lighting.pointLight.linearAttenuation == 0.0f && lighting.pointLight.quadraticAttenuation == 0.0f)
        {
            throw std::runtime_error("Point light attenuation cannot be entirely zero!");
        }

        m_Lighting = lighting;
    }
}