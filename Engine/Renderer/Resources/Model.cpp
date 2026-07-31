#include "Renderer/Resources/Model.h"
#include "Renderer/Resources/Mesh.h"
#include "Renderer/Resources/Material.h"

#include <stdexcept>
#include <utility>

namespace Kosmos
{
    Model::Model(std::vector<ModelPart> parts)
        : m_Parts(std::move(parts))
    {
        if (m_Parts.empty())
        {
            throw std::runtime_error("Cannot create a model without parts!");
        }

        for (const ModelPart& part : m_Parts)
        {
            if (!part.mesh || !part.material)
            {
                throw std::runtime_error("Model part requires both a mesh and a material!");
            }
        }
    }
}