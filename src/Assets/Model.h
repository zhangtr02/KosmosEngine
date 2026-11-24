#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <string>
#include <vector>

enum MaterialTextureSlot : size_t
{
    BaseColorSlot,
    NormalSlot,
    MetallicRoughnessSlot,
    OcclusionSlot,
    EmissiveSlot,
    MaterialTextureCount
};

struct Vertex
{
    glm::vec3 position;
    std::array<glm::vec2, MaterialTextureCount> uv{};
    glm::vec3 normal{0.0f};
    glm::vec4 tangent{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 color{1.0f};
};

struct EncodedImage
{
    std::shared_ptr<const uint8_t> data;
    size_t size{};
    bool operator==(const EncodedImage&) const = default;
};

enum class TextureEncoding
{
    Standard,
    BasisKtx2,
    WebP
};

struct TextureSource
{
    std::filesystem::path path;
    EncodedImage embedded;
    TextureEncoding encoding = TextureEncoding::Standard;
    bool srgb = true;
    bool flatNormal = false; // Fallback: flat normal when true, otherwise white.
    bool hasImage() const { return !path.empty() || embedded.data; }
    bool operator==(const TextureSource& other) const
    {
        return path == other.path && embedded == other.embedded && encoding == other.encoding &&
               srgb == other.srgb && (hasImage() || flatNormal == other.flatNormal);
    }
};

enum class AlphaMode
{
    Opaque,
    Mask,
    Blend
};

enum class TextureWrap
{
    Repeat,
    MirroredRepeat,
    ClampToEdge
};

enum class MipFilter
{
    None,
    Nearest,
    Linear
};

struct TextureSampler
{
    TextureWrap wrapU = TextureWrap::Repeat;
    TextureWrap wrapV = TextureWrap::Repeat;
    MipFilter mip = MipFilter::Linear;
    bool nearestMin = false;
    bool nearestMag = false;
    bool operator==(const TextureSampler&) const = default;
};

struct MaterialTexture
{
    uint32_t image{};
    TextureSampler sampler;
};

struct Material
{
    glm::vec4 baseColorFactor{1.0f};
    glm::vec3 emissiveFactor{0.0f};
    float emissiveStrength = 1.0f;
    std::array<MaterialTexture, MaterialTextureCount> textures;
    float normalScale = 0.0f;
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float occlusionStrength = 1.0f;
    float alphaCutoff = 0.5f;
    AlphaMode alphaMode = AlphaMode::Opaque;
    bool doubleSided = false;
    bool unlit = false;
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
