#pragma once

#include "Renderer/Resources/Vertex.h"

#include <cstdint>
#include <vector>

namespace Kosmos
{
    class Mesh
    {
        public:
            Mesh(std::vector<Vertex> vertices, std::vector<uint32_t> indices);

            const std::vector<Vertex>& GetVertices() const { return m_Vertices; }
            const std::vector<uint32_t>& GetIndices() const { return m_Indices; }

        private:
            std::vector<Vertex> m_Vertices;
            std::vector<uint32_t> m_Indices;
    };
}