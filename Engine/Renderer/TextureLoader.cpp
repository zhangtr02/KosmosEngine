#include "Renderer/TextureLoader.h"

#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <array>
#include <utility>

namespace Kosmos
{
    std::shared_ptr<Texture> TextureLoader::Load(const std::filesystem::path& path, TextureColorSpace colorSpace)
    {
        std::ifstream file(path, std::ios::ate | std::ios::binary);

        if (!file.is_open())
        {
            throw std::runtime_error("Failed to open texture file: " + path.string());
        }

        const std::streamsize encodedSize = file.tellg();

        if (encodedSize <= 0 || encodedSize > std::numeric_limits<int>::max())
        {
            throw std::runtime_error("Texture file size is invalid: " + path.string());
        }

        std::vector<uint8_t> encodedData(static_cast<size_t>(encodedSize));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(encodedData.data()), encodedSize);

        if (!file)
        {
            throw std::runtime_error("Failed to read texture file: " + path.string());
        }

        int width = 0;
        int height = 0;
        int sourceChannelCount = 0;

        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> decodedPixels(
            stbi_load_from_memory(encodedData.data(), static_cast<int>(encodedData.size()), &width, &height, &sourceChannelCount, STBI_rgb_alpha),
            &stbi_image_free);

        if (!decodedPixels)
        {
            std::string message = "Failed to decode texture file: " + path.string();
            const char* reason = stbi_failure_reason();

            if (reason)
            {
                message += ": ";
                message += reason;
            }

            throw std::runtime_error(message);
        }

        if (width <= 0 || height <= 0)
        {
            throw std::runtime_error("Decoded texture has an invalid extent: " + path.string());
        }

        const size_t pixelDataSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
        std::vector<uint8_t> pixels(decodedPixels.get(), decodedPixels.get() + pixelDataSize);

        return std::make_shared<Texture>(static_cast<uint32_t>(width), static_cast<uint32_t>(height), std::move(pixels), colorSpace);
    }

    std::shared_ptr<CubeTexture> TextureLoader::LoadCube(const std::array<std::filesystem::path, CubeTexture::FaceCount>& paths, TextureColorSpace colorSpace)
    {
        CubeTexture::Faces faces;

        for (uint32_t faceIndex = 0; faceIndex < CubeTexture::FaceCount; ++faceIndex)
        {
            faces[faceIndex] = Load(paths[faceIndex], colorSpace);
        }

        return std::make_shared<CubeTexture>(std::move(faces));
    }
}