#pragma once

#include <cstddef>
#include <cstdint>
#include <glm/vec4.hpp>
#include <memory>
#include <vector>

struct TextureSource;
struct EnvironmentSource;

struct DecodedTexture
{
    uint32_t width, height;
    std::shared_ptr<const uint8_t> pixels;
    size_t bytes;
    std::vector<size_t> mipOffsets;
};

DecodedTexture decodeTexture(const TextureSource& source, uint32_t maxDimension);

struct DecodedEnvironment
{
    uint32_t width{}, height{};
    std::vector<glm::vec4> radiance;
    std::vector<size_t> mipOffsets;
    std::vector<glm::vec4> irradiance;
};

DecodedEnvironment decodeEnvironment(const EnvironmentSource& source, uint32_t maxDimension);
