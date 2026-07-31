#include "Renderer/Loaders/ObjLoader.h"
#include "Renderer/Resources/Material.h"
#include "Renderer/Resources/Mesh.h"
#include "Renderer/Resources/Vertex.h"

#include <glm/geometric.hpp>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include <cmath>

namespace Kosmos
{
    namespace
    {
        struct ParsedVertexIndex
        {
            int position = 0;
            std::optional<int> textureCoordinate;
            std::optional<int> normal;
        };

        struct VertexKey
        {
            uint32_t position = 0;
            int32_t textureCoordinate = -1;
            uint32_t normal = 0;

            bool operator==(const VertexKey&) const = default;
        };

        struct VertexKeyHash
        {
            size_t operator()(const VertexKey& key) const
            {
                size_t result = std::hash<uint32_t>{}(key.position);
                result ^= std::hash<int32_t>{}(key.textureCoordinate) + 0x9e3779b9 + (result << 6) + (result >> 2);
                result ^= std::hash<uint32_t>{}(key.normal) + 0x9e3779b9 + (result << 6) + (result >> 2);
                return result;
            }
        };

        struct MeshBuilder
        {
            std::shared_ptr<Material> material;
            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;
            std::unordered_map<VertexKey, uint32_t, VertexKeyHash> vertexIndices;
        };

        std::runtime_error MakeObjError(const std::filesystem::path& path, size_t lineNumber, const std::string& message)
        {
            return std::runtime_error("OBJ error in " + path.string() + " at line " + std::to_string(lineNumber) + ": " + message);
        }

        int ParseInteger(std::string_view text, const std::filesystem::path& path, size_t lineNumber)
        {
            int value = 0;
            const char* begin = text.data();
            const char* end = begin + text.size();
            const std::from_chars_result result = std::from_chars(begin, end, value);

            if (text.empty() || result.ec != std::errc{} || result.ptr != end)
            {
                throw MakeObjError(path, lineNumber, "invalid face index");
            }

            return value;
        }

        ParsedVertexIndex ParseVertexIndex(std::string_view token, const std::filesystem::path& path, size_t lineNumber)
        {
            const size_t firstSlash = token.find('/');
            const std::string_view positionText = token.substr(0, firstSlash);
            ParsedVertexIndex index{};
            index.position = ParseInteger(positionText, path, lineNumber);

            if (firstSlash == std::string_view::npos)
            {
                return index;
            }

            const size_t secondSlash = token.find('/', firstSlash + 1);
            const size_t textureCoordinateEnd = secondSlash == std::string_view::npos ? token.size() : secondSlash;
            const std::string_view textureCoordinateText = token.substr(firstSlash + 1, textureCoordinateEnd - firstSlash - 1);

            if (!textureCoordinateText.empty())
            {
                index.textureCoordinate = ParseInteger(textureCoordinateText, path, lineNumber);
            }

            if (secondSlash != std::string_view::npos)
            {
                const std::string_view normalText = token.substr(secondSlash + 1);

                if (!normalText.empty())
                {
                    index.normal = ParseInteger(normalText, path, lineNumber);
                }
            }

            return index;
        }

        uint32_t ResolveIndex(int index, size_t elementCount, const std::filesystem::path& path, size_t lineNumber)
        {
            if (index == 0)
            {
                throw MakeObjError(path, lineNumber, "OBJ indices cannot be zero");
            }

            const int64_t resolvedIndex = index > 0 ? static_cast<int64_t>(index) - 1 : static_cast<int64_t>(elementCount) + index;

            if (resolvedIndex < 0 || resolvedIndex >= static_cast<int64_t>(elementCount))
            {
                throw MakeObjError(path, lineNumber, "face index is outside the source array");
            }

            return static_cast<uint32_t>(resolvedIndex);
        }

        std::shared_ptr<Material> ResolveMaterial(const std::string& name, const ObjLoader::MaterialMap& materials, const std::shared_ptr<Material>& defaultMaterial, const std::filesystem::path& path, size_t lineNumber)
        {
            if (name.empty())
            {
                return defaultMaterial;
            }

            const auto iterator = materials.find(name);

            if (iterator == materials.end() || !iterator->second)
            {
                throw MakeObjError(path, lineNumber, "material '" + name + "' was not provided");
            }

            return iterator->second;
        }

        MeshBuilder& GetMeshBuilder(const std::string& materialName, std::shared_ptr<Material> material, std::vector<MeshBuilder>& builders, std::unordered_map<std::string, size_t>& builderIndices)
        {
            const auto iterator = builderIndices.find(materialName);

            if (iterator != builderIndices.end())
            {
                return builders[iterator->second];
            }

            const size_t builderIndex = builders.size();
            builderIndices.emplace(materialName, builderIndex);
            builders.push_back({std::move(material)});
            return builders.back();
        }

        uint32_t GetVertexIndex(const ParsedVertexIndex& sourceIndex, const std::vector<glm::vec3>& positions, const std::vector<glm::vec2>& textureCoordinates, const std::vector<glm::vec3>& normals, MeshBuilder& builder, const std::filesystem::path& path, size_t lineNumber)
        {
            if (!sourceIndex.normal)
            {
                throw MakeObjError(path, lineNumber, "basic lighting requires every face vertex to have a normal");
            }

            const uint32_t positionIndex = ResolveIndex(sourceIndex.position, positions.size(), path, lineNumber);
            const uint32_t normalIndex = ResolveIndex(*sourceIndex.normal, normals.size(), path, lineNumber);
            int32_t textureCoordinateIndex = -1;

            if (sourceIndex.textureCoordinate)
            {
                textureCoordinateIndex = static_cast<int32_t>(ResolveIndex(*sourceIndex.textureCoordinate, textureCoordinates.size(), path, lineNumber));
            }

            const VertexKey key{positionIndex, textureCoordinateIndex, normalIndex};
            const auto iterator = builder.vertexIndices.find(key);

            if (iterator != builder.vertexIndices.end())
            {
                return iterator->second;
            }

            const glm::vec2 textureCoordinate = textureCoordinateIndex >= 0 ? textureCoordinates[textureCoordinateIndex] : glm::vec2(0.0f);
            const uint32_t vertexIndex = static_cast<uint32_t>(builder.vertices.size());
            builder.vertices.push_back({positions[positionIndex], glm::vec3(1.0f), textureCoordinate, normals[normalIndex], glm::vec4(0.0f)});
            builder.vertexIndices.emplace(key, vertexIndex);
            return vertexIndex;
        }

        glm::vec3 CreateFallbackTangent(const glm::vec3& normal)
        {
            const glm::vec3 referenceAxis = std::abs(normal.z) < 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
            return glm::normalize(glm::cross(referenceAxis, normal));
        }

        void GenerateTangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
        {
            constexpr float epsilon = 0.00000001f;
            std::vector<glm::vec3> tangentSums(vertices.size(), glm::vec3(0.0f));
            std::vector<glm::vec3> bitangentSums(vertices.size(), glm::vec3(0.0f));

            for (size_t index = 0; index + 2 < indices.size(); index += 3)
            {
                const uint32_t firstIndex = indices[index];
                const uint32_t secondIndex = indices[index + 1];
                const uint32_t thirdIndex = indices[index + 2];

                const Vertex& first = vertices[firstIndex];
                const Vertex& second = vertices[secondIndex];
                const Vertex& third = vertices[thirdIndex];

                const glm::vec3 firstEdge = second.position - first.position;
                const glm::vec3 secondEdge = third.position - first.position;
                const glm::vec2 firstUvEdge = second.textureCoordinate - first.textureCoordinate;
                const glm::vec2 secondUvEdge = third.textureCoordinate - first.textureCoordinate;
                const float determinant = firstUvEdge.x * secondUvEdge.y - firstUvEdge.y * secondUvEdge.x;

                if (std::abs(determinant) <= epsilon)
                {
                    continue;
                }

                const float inverseDeterminant = 1.0f / determinant;
                const glm::vec3 tangent = (firstEdge * secondUvEdge.y - secondEdge * firstUvEdge.y) * inverseDeterminant;
                const glm::vec3 bitangent = (secondEdge * firstUvEdge.x - firstEdge * secondUvEdge.x) * inverseDeterminant;

                tangentSums[firstIndex] += tangent;
                tangentSums[secondIndex] += tangent;
                tangentSums[thirdIndex] += tangent;
                bitangentSums[firstIndex] += bitangent;
                bitangentSums[secondIndex] += bitangent;
                bitangentSums[thirdIndex] += bitangent;
            }

            for (size_t vertexIndex = 0; vertexIndex < vertices.size(); ++vertexIndex)
            {
                Vertex& vertex = vertices[vertexIndex];
                const glm::vec3 normal = glm::normalize(vertex.normal);
                glm::vec3 tangent = tangentSums[vertexIndex] - normal * glm::dot(normal, tangentSums[vertexIndex]);

                if (glm::dot(tangent, tangent) <= epsilon)
                {
                    tangent = CreateFallbackTangent(normal);
                }
                else
                {
                    tangent = glm::normalize(tangent);
                }

                const glm::vec3 bitangent = bitangentSums[vertexIndex];
                const float handedness = glm::dot(bitangent, bitangent) > epsilon && glm::dot(glm::cross(normal, tangent), bitangent) < 0.0f ? -1.0f : 1.0f;
                vertex.tangent = glm::vec4(tangent, handedness);
            }
        }
    }

    Model ObjLoader::Load(const std::filesystem::path& path, const MaterialMap& materials, std::shared_ptr<Material> defaultMaterial)
    {
        if (!defaultMaterial)
        {
            throw std::runtime_error("OBJ loader requires a default material!");
        }

        std::ifstream file(path);

        if (!file.is_open())
        {
            throw std::runtime_error("Failed to open OBJ file: " + path.string());
        }

        std::vector<glm::vec3> positions;
        std::vector<glm::vec2> textureCoordinates;
        std::vector<glm::vec3> normals;
        std::vector<MeshBuilder> builders;
        std::unordered_map<std::string, size_t> builderIndices;
        std::string currentMaterialName;
        std::string line;
        size_t lineNumber = 0;

        while (std::getline(file, line))
        {
            ++lineNumber;

            std::istringstream lineStream(line);
            std::string type;
            lineStream >> type;

            if (type.empty() || type[0] == '#')
            {
                continue;
            }

            if (type == "v")
            {
                glm::vec3 position{};

                if (!(lineStream >> position.x >> position.y >> position.z))
                {
                    throw MakeObjError(path, lineNumber, "invalid vertex position");
                }

                positions.push_back(position);
            }
            else if (type == "vt")
            {
                glm::vec2 textureCoordinate{};

                if (!(lineStream >> textureCoordinate.x >> textureCoordinate.y))
                {
                    throw MakeObjError(path, lineNumber, "invalid texture coordinate");
                }

                textureCoordinate.y = 1.0f - textureCoordinate.y;
                textureCoordinates.push_back(textureCoordinate);
            }
            else if (type == "vn")
            {
                glm::vec3 normal{};

                if (!(lineStream >> normal.x >> normal.y >> normal.z))
                {
                    throw MakeObjError(path, lineNumber, "invalid vertex normal");
                }

                if (glm::dot(normal, normal) <= 0.0f)
                {
                    throw MakeObjError(path, lineNumber, "vertex normal cannot be zero");
                }

                normals.push_back(glm::normalize(normal));
            }
            else if (type == "usemtl")
            {
                if (!(lineStream >> currentMaterialName))
                {
                    throw MakeObjError(path, lineNumber, "usemtl requires a material name");
                }

                ResolveMaterial(currentMaterialName, materials, defaultMaterial, path, lineNumber);
            }
            else if (type == "f")
            {
                const std::shared_ptr<Material> material = ResolveMaterial(currentMaterialName, materials, defaultMaterial, path, lineNumber);
                MeshBuilder& builder = GetMeshBuilder(currentMaterialName, material, builders, builderIndices);
                std::vector<uint32_t> faceIndices;
                std::string token;

                while (lineStream >> token)
                {
                    if (!token.empty() && token[0] == '#')
                    {
                        break;
                    }

                    const ParsedVertexIndex sourceIndex = ParseVertexIndex(token, path, lineNumber);
                    faceIndices.push_back(GetVertexIndex(sourceIndex, positions, textureCoordinates, normals, builder, path, lineNumber));
                }

                if (faceIndices.size() < 3)
                {
                    throw MakeObjError(path, lineNumber, "face requires at least three vertices");
                }

                for (size_t index = 1; index + 1 < faceIndices.size(); ++index)
                {
                    builder.indices.push_back(faceIndices[0]);
                    builder.indices.push_back(faceIndices[index]);
                    builder.indices.push_back(faceIndices[index + 1]);
                }
            }
        }

        std::vector<ModelPart> parts;
        parts.reserve(builders.size());

        for (MeshBuilder& builder : builders)
        {
            if (!builder.indices.empty())
            {
                GenerateTangents(builder.vertices, builder.indices);
                parts.push_back({std::make_shared<Mesh>(std::move(builder.vertices), std::move(builder.indices)), std::move(builder.material)});
            }
        }

        if (parts.empty())
        {
            throw std::runtime_error("OBJ file does not contain any drawable faces: " + path.string());
        }

        return Model(std::move(parts));
    }
}