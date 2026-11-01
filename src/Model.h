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
    glm::vec4 tangent{0.0f, 0.0f, 0.0f, 1.0f};
};

struct TextureSource
{
    std::filesystem::path path; // Empty: white for sRGB, flat normal for linear.
    bool srgb = true;
    bool operator==(const TextureSource&) const = default;
};

struct Material
{
    glm::vec4 baseColorFactor{1.0f};
    uint32_t textureIndex{};
    uint32_t normalTextureIndex{};
    float normalScale = 0.0f;
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
    std::vector<TextureSource> textures;
    std::vector<Material> materials;
    std::vector<Draw> draws;
};

// Static indexed triangles, with baked node transforms and opaque base-color materials.
Model loadModel(const std::filesystem::path& path);
