#include "Texture.h"

#include "Model.h"

#include <algorithm>
#include <basisu/transcoder/basisu_transcoder.h>
#include <bit>
#include <fstream>
#include <limits>
#include <mutex>
#include <span>
#include <stb_image.h>
#include <stdexcept>
#include <webp/decode.h>

namespace
{
constexpr size_t rgbaBytes = 4;

void checkDimensions(uint32_t width, uint32_t height, uint32_t maxDimension)
{
    if (!width || !height || width > maxDimension || height > maxDimension)
        throw std::runtime_error("Texture dimensions exceed device limits");
}

DecodedTexture decodeStandard(const TextureSource& source, uint32_t maxDimension)
{
    int width = 0, height = 0;
    auto* data =
        source.embedded.data
            ? stbi_load_from_memory(source.embedded.data.get(),
                                    static_cast<int>(source.embedded.size), &width, &height,
                                    nullptr, STBI_rgb_alpha)
            : stbi_load(pathUtf8(source.path).c_str(), &width, &height, nullptr, STBI_rgb_alpha);
    std::shared_ptr<const uint8_t> pixels(data, stbi_image_free);
    if (!pixels)
    {
        const auto* reason = stbi_failure_reason();
        throw std::runtime_error(reason ? reason : "PNG/JPEG decoding failed");
    }
    checkDimensions(width, height, maxDimension);
    return {static_cast<uint32_t>(width),
            static_cast<uint32_t>(height),
            pixels,
            static_cast<size_t>(width) * height * rgbaBytes,
            {}};
}

DecodedTexture decodeWebP(std::span<const uint8_t> encoded, uint32_t maxDimension)
{
    WebPBitstreamFeatures features{};
    if (WebPGetFeatures(encoded.data(), encoded.size(), &features) != VP8_STATUS_OK ||
        features.has_animation)
        throw std::runtime_error("Invalid or animated WebP image");
    checkDimensions(features.width, features.height, maxDimension);
    auto* data = WebPDecodeRGBA(encoded.data(), encoded.size(), nullptr, nullptr);
    std::shared_ptr<const uint8_t> pixels(data, WebPFree);
    if (!pixels)
        throw std::runtime_error("WebP decoding failed");
    return {static_cast<uint32_t>(features.width),
            static_cast<uint32_t>(features.height),
            pixels,
            static_cast<size_t>(features.width) * features.height * rgbaBytes,
            {}};
}

DecodedTexture decodeBasis(std::span<const uint8_t> encoded, uint32_t maxDimension)
{
    static std::once_flag initialized;
    std::call_once(initialized, basist::basisu_transcoder_init);

    basist::ktx2_transcoder decoder;
    if (!decoder.init(encoded.data(), static_cast<uint32_t>(encoded.size())) ||
        (!decoder.is_etc1s() && !decoder.is_uastc()) || decoder.get_faces() != 1 ||
        decoder.get_layers() != 0)
        throw std::runtime_error("Expected a 2D ETC1S or UASTC KTX2 texture");

    DecodedTexture texture{decoder.get_width(), decoder.get_height(), {}, 0, {}};
    checkDimensions(texture.width, texture.height, maxDimension);
    const uint32_t mipLevels = decoder.get_levels();
    const auto maxLevels =
        static_cast<uint32_t>(std::bit_width(std::max(texture.width, texture.height)));
    if (!mipLevels || mipLevels > maxLevels || !decoder.start_transcoding())
        throw std::runtime_error("Invalid KTX2 mip chain or Basis data");

    for (uint32_t level = 0; level < mipLevels; ++level)
    {
        const uint64_t texels =
            uint64_t(std::max(texture.width >> level, 1u)) * std::max(texture.height >> level, 1u);
        if (texels > UINT32_MAX ||
            texels > (std::numeric_limits<size_t>::max() - texture.bytes) / rgbaBytes)
            throw std::runtime_error("KTX2 image is too large");
        texture.mipOffsets.push_back(texture.bytes);
        texture.bytes += static_cast<size_t>(texels) * rgbaBytes;
    }
    auto pixels = std::make_shared_for_overwrite<uint8_t[]>(texture.bytes);
    for (uint32_t level = 0; level < mipLevels; ++level)
    {
        const size_t offset = texture.mipOffsets[level];
        const size_t end = level + 1 < mipLevels ? texture.mipOffsets[level + 1] : texture.bytes;
        if (!decoder.transcode_image_level(level, 0, 0, pixels.get() + offset,
                                           static_cast<uint32_t>((end - offset) / rgbaBytes),
                                           basist::transcoder_texture_format::cTFRGBA32))
            throw std::runtime_error("Basis mip transcoding failed");
    }
    texture.pixels = {pixels, pixels.get()};
    return texture;
}
} // namespace

DecodedTexture decodeTexture(const TextureSource& source, uint32_t maxDimension)
{
    try
    {
        if (source.encoding == TextureEncoding::Standard)
            return decodeStandard(source, maxDimension);

        std::vector<uint8_t> fileBytes;
        std::span<const uint8_t> encoded(source.embedded.data.get(), source.embedded.size);
        if (encoded.empty())
        {
            std::ifstream file(source.path, std::ios::binary | std::ios::ate);
            const auto size = file.tellg();
            if (size <= 0 || size > INT32_MAX)
                throw std::runtime_error("Invalid texture file size");
            fileBytes.resize(static_cast<size_t>(size));
            file.seekg(0);
            if (!file.read(reinterpret_cast<char*>(fileBytes.data()), size))
                throw std::runtime_error("Failed to read texture file");
            encoded = fileBytes;
        }

        return source.encoding == TextureEncoding::BasisKtx2 ? decodeBasis(encoded, maxDimension)
                                                             : decodeWebP(encoded, maxDimension);
    }
    catch (const std::runtime_error& error)
    {
        const std::string prefix =
            source.embedded.data ? "Cannot load embedded texture: " : "Cannot load texture: ";
        throw std::runtime_error(prefix + pathUtf8(source.path) + " (" + error.what() + ")");
    }
}
