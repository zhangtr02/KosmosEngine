#include "Texture.h"

#include "Model.h"

#include <algorithm>
#include <basisu/transcoder/basisu_transcoder.h>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <mutex>
#include <numbers>
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

namespace
{
glm::vec3 panoramaDirection(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    constexpr float pi = std::numbers::pi_v<float>;
    const float phi = 2.0f * pi * ((float(x) + 0.5f) / float(width) - 0.5f);
    const float theta = pi * (float(y) + 0.5f) / float(height);
    return {std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
}

glm::vec3 sampleCube(const std::array<std::vector<glm::vec3>, 6>& faces, uint32_t size,
                     glm::vec3 direction)
{
    const auto [x, y, z] = std::array{direction.x, direction.y, direction.z};
    const auto a = glm::abs(direction);
    const float major = std::max({a.x, a.y, a.z});
    size_t face;
    glm::vec2 uv;
    if (major == a.x)
    {
        face = x >= 0 ? 0 : 1;
        uv = {x >= 0 ? -z : z, -y};
    }
    else if (major == a.y)
    {
        face = y >= 0 ? 2 : 3;
        uv = {x, y >= 0 ? z : -z};
    }
    else
    {
        face = z >= 0 ? 4 : 5;
        uv = {z >= 0 ? x : -x, -y};
    }
    // Exported glTF faces have the opposite vertical origin from cube sampling.
    const auto texel = (uv * glm::vec2(1, -1) / major * 0.5f + 0.5f) * float(size) - 0.5f;
    const auto base = glm::ivec2(glm::floor(texel));
    const auto weight = glm::fract(texel);
    const auto at = [&](int dx, int dy)
    {
        const auto p =
            glm::clamp(base + glm::ivec2(dx, dy), glm::ivec2(0), glm::ivec2(int(size) - 1));
        return faces[face][size_t(p.y) * size + p.x];
    };
    return glm::mix(glm::mix(at(0, 0), at(1, 0), weight.x), glm::mix(at(0, 1), at(1, 1), weight.x),
                    weight.y);
}
} // namespace

DecodedEnvironment decodeEnvironment(const EnvironmentSource& source, uint32_t maxDimension)
{
    checkDimensions(source.faceSize, source.faceSize, maxDimension);
    DecodedEnvironment result;
    result.width = static_cast<uint32_t>(
        std::min<uint64_t>(4ull * source.faceSize, std::bit_floor(maxDimension)));
    result.height = result.width / 2;
    const auto rotation = glm::conjugate(source.rotation);
    const auto linear = [](uint8_t value)
    {
        const float srgb = value / 255.0f;
        return srgb <= 0.04045f ? srgb / 12.92f : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
    };
    for (size_t level = 0; level < source.mips.size(); ++level)
    {
        const auto size = source.faceSize >> level;
        std::array<std::vector<glm::vec3>, 6> faces;
        for (size_t face = 0; face < faces.size(); ++face)
        {
            const auto image = decodeTexture(source.mips[level][face], maxDimension);
            if (image.width != size || image.height != size)
                throw std::runtime_error("Image-based light face size does not match its mip");
            faces[face].resize(size_t(size) * size);
            for (size_t i = 0; i < faces[face].size(); ++i)
            {
                const auto* p = image.pixels.get() + i * rgbaBytes;
                // Opaque RGB images are expanded with alpha=255 and follow the same decode.
                const float range = 255.0f / std::max<uint8_t>(p[3], 1);
                faces[face][i] = glm::vec3(linear(p[0]), linear(p[1]), linear(p[2])) * range;
            }
        }
        const auto width = std::max(result.width >> level, 1u);
        const auto height = std::max(result.height >> level, 1u);
        result.mipOffsets.push_back(result.radiance.size() * sizeof(glm::vec4));
        for (uint32_t y = 0; y < height; ++y)
            for (uint32_t x = 0; x < width; ++x)
                result.radiance.emplace_back(
                    source.intensity *
                        sampleCube(faces, size, rotation * panoramaDirection(x, y, width, height)),
                    1.0f);
    }
    for (uint32_t y = 0; y < 32; ++y)
        for (uint32_t x = 0; x < 64; ++x)
        {
            const auto d = rotation * panoramaDirection(x, y, 64, 32);
            // Real SH basis, ordered by band and then by m.
            const std::array basis{0.282095f,
                                   -0.488603f * d.y,
                                   0.488603f * d.z,
                                   -0.488603f * d.x,
                                   1.092548f * d.x * d.y,
                                   -1.092548f * d.y * d.z,
                                   0.315392f * (3.0f * d.z * d.z - 1.0f),
                                   -1.092548f * d.x * d.z,
                                   0.546274f * (d.x * d.x - d.y * d.y)};
            glm::vec3 sum(0);
            for (size_t i = 0; i < basis.size(); ++i)
                sum += source.irradiance[i] * basis[i];
            result.irradiance.emplace_back(glm::max(sum * source.intensity, glm::vec3(0)), 1.0f);
        }
    return result;
}
