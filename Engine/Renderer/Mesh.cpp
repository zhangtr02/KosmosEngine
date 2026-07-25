#include "Renderer/Mesh.h"

#include <stdexcept>
#include <utility>

namespace Kosmos
{
    Mesh::Mesh(std::vector<Vertex> vertices, std::vector<uint32_t> indices)
        : m_Vertices(std::move(vertices)), m_Indices(std::move(indices))
    {
        if (m_Vertices.empty())
        {
            throw std::runtime_error("Cannot create a mesh without vertices!");
        }

        if (m_Indices.empty())
        {
            throw std::runtime_error("Cannot create a mesh without indices!");
        }

        for (uint32_t index : m_Indices)
        {
            if (index >= m_Vertices.size())
            {
                throw std::runtime_error("Mesh index is outside the vertex array!");
            }
        }
    }
}