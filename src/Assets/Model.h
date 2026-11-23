#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <string>
#include <vector>

struct Vertex
{
    glm::vec3 position;
    glm::vec2 uv{0.0f};
    glm::vec3 normal{0.0f};
    glm::vec4 tangent{0.0f, 0.0f, 0.0f, 1.0f};
};

struct EncodedImage
{
    std::shared_ptr<const uint8_t> data;
    size_t size{};
    bool operator==(const EncodedImage&) const = default;
};

struct TextureSource
{
    std::filesystem::path path;
    EncodedImage embedded;
    bool srgb = true;
    bool flatNormal = false; // Fallback: flat normal when true, otherwise white.
    bool hasImage() const { return !path.empty() || embedded.data; }
    bool operator==(const TextureSource& other) const
    {
        return path == other.path && embedded == other.embedded && srgb == other.srgb &&
               (hasImage() || flatNormal == other.flatNormal);
    }
};

enum class AlphaMode
{
    Opaque,
    Mask,
    Blend
};

struct Material
{
    glm::vec4 baseColorFactor{1.0f};
    glm::vec3 emissiveFactor{0.0f};
    float emissiveStrength = 1.0f;
    uint32_t textureIndex{};
    uint32_t normalTextureIndex{};
    uint32_t metallicRoughnessTextureIndex{};
    uint32_t occlusionTextureIndex{};
    uint32_t emissiveTextureIndex{};
    float normalScale = 0.0f;
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float occlusionStrength = 1.0f;
    float alphaCutoff = 0.5f;
    AlphaMode alphaMode = AlphaMode::Opaque;
    bool doubleSided = false;
};

struct Draw
{
    uint32_t firstIndex{};
    uint32_t indexCount{};
    int32_t vertexOffset{};
    uint32_t materialIndex{};
    glm::vec3 center{}; // Bounds center in model space, for transparency sorting.
};

struct ModelBounds
{
    glm::vec3 minimum;
    glm::vec3 maximum;
};

struct Model
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<TextureSource> textures;
    std::vector<Material> materials;
    std::vector<Draw> draws;
    ModelBounds bounds{};
    glm::vec3 center{};
    float radius{};
};

// Static triangles, with baked node transforms and glTF metallic-roughness materials.
enum class ImportStage
{
    Copying,
    Reading,
    BuildingMeshes,
    GeneratingTangents,
    UploadingGeometry,
    UploadingTextures,
    Finalizing
};

using ImportProgress = std::function<void(ImportStage)>;

Model loadModel(const std::filesystem::path& path, const ImportProgress& progress = {});
std::filesystem::path copyModel(const std::filesystem::path& source,
                                const std::filesystem::path& directory,
                                const ImportProgress& progress = {});

inline std::string pathUtf8(const std::filesystem::path& path)
{
    const auto utf8 = path.u8string();
    return {utf8.begin(), utf8.end()};
}
