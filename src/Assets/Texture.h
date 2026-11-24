#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

struct TextureSource;

struct DecodedTexture
{
    uint32_t width, height;
    std::shared_ptr<const uint8_t> pixels;
    size_t bytes;
    std::vector<size_t> mipOffsets;
};

DecodedTexture decodeTexture(const TextureSource& source, uint32_t maxDimension);
