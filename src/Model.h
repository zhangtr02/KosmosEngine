#pragma once

#include <cstdint>
#include <filesystem>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <vector>

struct Vertex
{
    glm::vec3 position;
    glm::vec2 uv;
    glm::vec3 normal;
};

struct Material
{
    glm::vec4 baseColorFactor{1.0f};
    uint32_t textureIndex{};
};

struct Draw
{
    uint32_t firstIndex{};
    uint32_t indexCount{};
    int32_t vertexOffset{};
    uint32_t materialIndex{};
};

struct Model
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<std::filesystem::path> textures; // An empty path denotes a white texture.
    std::vector<Material> materials;
    std::vector<Draw> draws;
};

// Static indexed triangles, with baked node transforms and opaque base-color materials.
Model loadModel(const std::filesystem::path& path);
