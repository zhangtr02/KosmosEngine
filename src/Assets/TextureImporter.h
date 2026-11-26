#pragma once

#include <cstddef>
#include <cstdint>
#include <glm/vec4.hpp>
#include <memory>
#include <vector>

struct TextureSource;
struct EnvironmentSource;

enum class TextureFormat
{
    Rgba8,
    Bc1,
    Bc3,
    Bc5
};

struct DecodedTexture
{
    uint32_t width, height;
    std::shared_ptr<const uint8_t> data;
    size_t bytes;
    std::vector<size_t> mipOffsets;
    TextureFormat format = TextureFormat::Rgba8;
};

DecodedTexture decodeTexture(const TextureSource& source, uint32_t maxDimension);
bool hasDdsCutout(const TextureSource& source);

struct DecodedEnvironment
{
    uint32_t width{}, height{};
    std::vector<glm::vec4> radiance;
    std::vector<size_t> mipOffsets;
    std::vector<glm::vec4> irradiance;
};

DecodedEnvironment decodeEnvironment(const EnvironmentSource& source, uint32_t maxDimension);
