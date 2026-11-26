#include "Tangents.h"
#include "Model.h"

#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <mikktspace.h>

namespace
{
struct TangentMesh
{
    std::span<const Vertex> vertices;
    std::span<const uint32_t> indices;
    std::span<glm::vec4> tangents;
    float authoredHandednessScale;

    static const TangentMesh& from(const SMikkTSpaceContext* context)
    {
        return *static_cast<const TangentMesh*>(context->m_pUserData);
    }

    const Vertex& vertex(int face, int corner) const
    {
        return vertices[indices[face * 3 + corner]];
    }
};

} // namespace

bool generateTangents(std::vector<Vertex>& vertices, size_t base, std::span<uint32_t> indices,
                      float authoredHandednessScale)
{
    // MikkTSpace packs a face index and two corner bits into an int.
    if (indices.size() / 3 > std::numeric_limits<int>::max() / 4)
        return false;
    std::vector<glm::vec4> tangents(indices.size());
    const auto sourceVertices = std::span(vertices).subspan(base);
    TangentMesh mesh{sourceVertices, indices, tangents, authoredHandednessScale};
    SMikkTSpaceInterface interface{
        .m_getNumFaces =
            [](const SMikkTSpaceContext* context)
        {
            return static_cast<int>(TangentMesh::from(context).indices.size() / 3);
        },
        .m_getNumVerticesOfFace =
            [](const SMikkTSpaceContext*, int)
        {
            return 3;
        },
        .m_getPosition =
            [](const SMikkTSpaceContext* context, float* out, int face, int corner)
        {
            std::copy_n(glm::value_ptr(TangentMesh::from(context).vertex(face, corner).position), 3,
                        out);
        },
        .m_getNormal =
            [](const SMikkTSpaceContext* context, float* out, int face, int corner)
        {
            std::copy_n(glm::value_ptr(TangentMesh::from(context).vertex(face, corner).normal), 3,
                        out);
        },
        .m_getTexCoord =
            [](const SMikkTSpaceContext* context, float* out, int face, int corner)
        {
            std::copy_n(
                glm::value_ptr(TangentMesh::from(context).vertex(face, corner).uv[NormalSlot]), 2,
                out);
        },
        .m_setTSpaceBasic =
            [](const SMikkTSpaceContext* context, const float* tangent, float sign, int face,
               int corner)
        {
            const auto& mesh = TangentMesh::from(context);
            if (mesh.authoredHandednessScale != 0.0f)
                sign = mesh.vertex(face, corner).tangent.w * mesh.authoredHandednessScale;
            mesh.tangents[face * 3 + corner] = glm::vec4(tangent[0], tangent[1], tangent[2], sign);
        }};
    const SMikkTSpaceContext context{&interface, &mesh};
    if (!genTangSpaceDefault(&context))
        return false;
    for (auto& vertex : sourceVertices)
        vertex.tangent.w = 0.0f;

    // The same input vertex may have multiple corner tangents at a UV seam.
    constexpr uint32_t noVertex = UINT32_MAX;
    std::vector<uint32_t> nextVertex(vertices.size() - base, noVertex);
    for (size_t corner = 0; corner < indices.size(); ++corner)
    {
        auto index = indices[corner];
        auto tangent = tangents[corner];
        const auto normal = vertices[base + index].normal;
        if (glm::length(glm::cross(normal, glm::vec3(tangent))) <= 1e-6f)
        {
            // Degenerate UVs can leave an arbitrary tangent parallel to the normal.
            const auto axis = std::abs(normal.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
            tangent = glm::vec4(glm::normalize(glm::cross(normal, axis)), tangent.w);
        }
        while (vertices[base + index].tangent.w != 0.0f &&
               vertices[base + index].tangent != tangent)
        {
            if (nextVertex[index] != noVertex)
            {
                index = nextVertex[index];
                continue;
            }
            if (vertices.size() == INT32_MAX)
                return false;
            nextVertex[index] = static_cast<uint32_t>(vertices.size() - base);
            vertices.push_back(vertices[base + index]);
            index = nextVertex[index];
            nextVertex.push_back(noVertex);
            break;
        }
        vertices[base + index].tangent = tangent;
        indices[corner] = index;
    }
    return true;
}
