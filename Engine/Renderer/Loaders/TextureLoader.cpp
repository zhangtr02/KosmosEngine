#include "Renderer/Loaders/TextureLoader.h"

#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_HDR
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr float Pi = 3.14159265359f;

    std::vector<uint8_t> ReadEncodedFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::ate | std::ios::binary);

        if (!file.is_open())
        {
            throw std::runtime_error("Failed to open texture file: " + path.string());
        }

        const std::streamsize size = file.tellg();

        if (size <= 0 || size > std::numeric_limits<int>::max())
        {
            throw std::runtime_error("Texture file size is invalid: " + path.string());
        }

        std::vector<uint8_t> data(static_cast<size_t>(size));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(data.data()), size);

        if (!file)
        {
            throw std::runtime_error("Failed to read texture file: " + path.string());
        }

        return data;
    }

    std::runtime_error CreateDecodeError(const std::filesystem::path& path)
    {
        std::string message = "Failed to decode texture file: " + path.string();
        if (const char* reason = stbi_failure_reason())
        {
            message += ": " + std::string(reason);
        }

        return std::runtime_error(message);
    }

    glm::vec3 GetCubeDirection(uint32_t faceIndex, float u, float v)
    {
        switch (faceIndex)
        {
            case 0: return glm::normalize(glm::vec3(1.0f, -v, -u));
            case 1: return glm::normalize(glm::vec3(-1.0f, -v, u));
            case 2: return glm::normalize(glm::vec3(u, 1.0f, v));
            case 3: return glm::normalize(glm::vec3(u, -1.0f, -v));
            case 4: return glm::normalize(glm::vec3(u, -v, 1.0f));
            default: return glm::normalize(glm::vec3(-u, -v, -1.0f));
        }
    }

    int WrapHorizontal(int value, int width)
    {
        value %= width;
        return value < 0 ? value + width : value;
    }

    glm::vec4 ReadFloatPixel(const float* pixels, int width, int x, int y)
    {
        const size_t index = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4;
        return glm::vec4(pixels[index], pixels[index + 1], pixels[index + 2], pixels[index + 3]);
    }

    glm::vec4 SampleEquirectangular(const float* pixels, int width, int height, const glm::vec3& direction)
    {
        const float u = std::atan2(direction.z, direction.x) / (2.0f * Pi) + 0.5f;
        const float v = 0.5f - std::asin(std::clamp(direction.y, -1.0f, 1.0f)) / Pi;
        const float sourceX = u * static_cast<float>(width) - 0.5f;
        const float sourceY = v * static_cast<float>(height) - 0.5f;
        const int x0 = static_cast<int>(std::floor(sourceX));
        const int y0 = static_cast<int>(std::floor(sourceY));
        const int x1 = x0 + 1;
        const int y1 = y0 + 1;
        const float interpolationX = sourceX - std::floor(sourceX);
        const float interpolationY = sourceY - std::floor(sourceY);
        const glm::vec4 topLeft = ReadFloatPixel(pixels, width, WrapHorizontal(x0, width), std::clamp(y0, 0, height - 1));
        const glm::vec4 topRight = ReadFloatPixel(pixels, width, WrapHorizontal(x1, width), std::clamp(y0, 0, height - 1));
        const glm::vec4 bottomLeft = ReadFloatPixel(pixels, width, WrapHorizontal(x0, width), std::clamp(y1, 0, height - 1));
        const glm::vec4 bottomRight = ReadFloatPixel(pixels, width, WrapHorizontal(x1, width), std::clamp(y1, 0, height - 1));
        const glm::vec4 top = topLeft + (topRight - topLeft) * interpolationX;
        const glm::vec4 bottom = bottomLeft + (bottomRight - bottomLeft) * interpolationX;
        return top + (bottom - top) * interpolationY;
    }
}

namespace Kosmos
{
    std::shared_ptr<Texture> TextureLoader::Load(const std::filesystem::path& path, TextureColorSpace colorSpace)
    {
        const std::vector<uint8_t> encodedData = ReadEncodedFile(path);
        int width = 0;
        int height = 0;
        int sourceChannelCount = 0;

        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> decodedPixels(stbi_load_from_memory(encodedData.data(), static_cast<int>(encodedData.size()), &width, &height, &sourceChannelCount, STBI_rgb_alpha), &stbi_image_free);

        if (!decodedPixels)
        {
            throw CreateDecodeError(path);
        }

        if (width <= 0 || height <= 0)
        {
            throw std::runtime_error("Decoded texture has an invalid extent: " + path.string());
        }

        const size_t elementCount = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
        std::vector<uint8_t> pixels(decodedPixels.get(), decodedPixels.get() + elementCount);
        return std::make_shared<Texture>(static_cast<uint32_t>(width), static_cast<uint32_t>(height), std::move(pixels), colorSpace);
    }

    std::shared_ptr<CubeTexture> TextureLoader::LoadCube(const std::array<std::filesystem::path, CubeTexture::FaceCount>& paths, TextureColorSpace colorSpace)
    {
        CubeTexture::Faces faces{};
        for (uint32_t faceIndex = 0; faceIndex < CubeTexture::FaceCount; ++faceIndex)
        {
            faces[faceIndex] = Load(paths[faceIndex], colorSpace);
        }

        return std::make_shared<CubeTexture>(std::move(faces));
    }

    std::shared_ptr<CubeTexture> TextureLoader::LoadHdrEquirectangular(const std::filesystem::path& path, uint32_t faceResolution)
    {
        if (faceResolution == 0)
        {
            throw std::runtime_error("HDR cubemap face resolution must be greater than zero!");
        }

        const std::vector<uint8_t> encodedData = ReadEncodedFile(path);

        if (stbi_is_hdr_from_memory(encodedData.data(), static_cast<int>(encodedData.size())) == 0)
        {
            throw std::runtime_error("Environment texture is not an HDR image: " + path.string());
        }

        int width = 0;
        int height = 0;
        int sourceChannelCount = 0;

        std::unique_ptr<float, decltype(&stbi_image_free)> decodedPixels(stbi_loadf_from_memory(encodedData.data(), static_cast<int>(encodedData.size()), &width, &height, &sourceChannelCount, STBI_rgb_alpha), &stbi_image_free);

        if (!decodedPixels)
        {
            throw CreateDecodeError(path);
        }

        if (width <= 0 || height <= 0 || width != height * 2)
        {
            throw std::runtime_error("HDR environment must use a 2:1 equirectangular layout: " + path.string());
        }

        CubeTexture::Faces faces{};

        for (uint32_t faceIndex = 0; faceIndex < CubeTexture::FaceCount; ++faceIndex)
        {
            std::vector<float> facePixels(static_cast<size_t>(faceResolution) * faceResolution * 4);

            for (uint32_t y = 0; y < faceResolution; ++y)
            {
                for (uint32_t x = 0; x < faceResolution; ++x)
                {
                    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(faceResolution) * 2.0f - 1.0f;
                    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(faceResolution) * 2.0f - 1.0f;
                    const glm::vec4 color = SampleEquirectangular(decodedPixels.get(), width, height, GetCubeDirection(faceIndex, u, v));
                    const size_t index = (static_cast<size_t>(y) * faceResolution + x) * 4;
                    facePixels[index] = color.r;
                    facePixels[index + 1] = color.g;
                    facePixels[index + 2] = color.b;
                    facePixels[index + 3] = 1.0f;
                }
            }

            faces[faceIndex] = std::make_shared<Texture>(faceResolution, faceResolution, std::move(facePixels));
        }

        return std::make_shared<CubeTexture>(std::move(faces));
    }
}
