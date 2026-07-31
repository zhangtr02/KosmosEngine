#include "Renderer/Lighting/EnvironmentLighting.h"
#include "Renderer/Resources/CubeTexture.h"
#include "Renderer/Resources/Texture.h"

#include <glm/geometric.hpp>
#include <array>
#include <cmath>

namespace
{
    constexpr float Pi = 3.14159265359f;

    float SRGBToLinear(float value)
    {
        return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
    }

    glm::vec3 DecodeColor(const Kosmos::Texture& texture, size_t pixelIndex)
    {
        if (texture.GetDataType() == Kosmos::TextureDataType::Float32)
        {
            const std::vector<float>& pixels = texture.GetFloatPixels();
            return glm::vec3(pixels[pixelIndex], pixels[pixelIndex + 1], pixels[pixelIndex + 2]);
        }

        const std::vector<uint8_t>& pixels = texture.GetBytePixels();
        glm::vec3 color(static_cast<float>(pixels[pixelIndex]) / 255.0f, static_cast<float>(pixels[pixelIndex + 1]) / 255.0f, static_cast<float>(pixels[pixelIndex + 2]) / 255.0f);

        if (texture.GetColorSpace() == Kosmos::TextureColorSpace::SRGB)
        {
            color.r = SRGBToLinear(color.r);
            color.g = SRGBToLinear(color.g);
            color.b = SRGBToLinear(color.b);
        }

        return color;
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

    std::array<float, 9> EvaluateSphericalHarmonics(const glm::vec3& direction)
    {
        const float x = direction.x;
        const float y = direction.y;
        const float z = direction.z;

        return {
            0.282095f,
            0.488603f * y,
            0.488603f * z,
            0.488603f * x,
            1.092548f * x * y,
            1.092548f * y * z,
            0.315392f * (3.0f * z * z - 1.0f),
            1.092548f * x * z,
            0.546274f * (x * x - y * y)
        };
    }
}

namespace Kosmos::EnvironmentLighting
{
    std::array<glm::vec4, DiffuseIrradianceCoefficientCount> ProjectDiffuseIrradiance(const CubeTexture& environment)
    {
        std::array<glm::vec3, DiffuseIrradianceCoefficientCount> coefficients{};
        float totalSolidAngle = 0.0f;

        for (uint32_t faceIndex = 0; faceIndex < CubeTexture::FaceCount; ++faceIndex)
        {
            const Texture& face = *environment.GetFaces()[faceIndex];
            const uint32_t width = face.GetWidth();
            const uint32_t height = face.GetHeight();

            for (uint32_t y = 0; y < height; ++y)
            {
                for (uint32_t x = 0; x < width; ++x)
                {
                    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width) * 2.0f - 1.0f;
                    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height) * 2.0f - 1.0f;
                    const glm::vec3 direction = GetCubeDirection(faceIndex, u, v);
                    const size_t pixelIndex = (static_cast<size_t>(y) * width + x) * 4;
                    const glm::vec3 radiance = DecodeColor(face, pixelIndex);

                    const float texelArea = 4.0f / static_cast<float>(width * height);
                    const float solidAngle = texelArea / std::pow(1.0f + u * u + v * v, 1.5f);
                    const std::array<float, 9> basis = EvaluateSphericalHarmonics(direction);

                    for (uint32_t coefficientIndex = 0; coefficientIndex < DiffuseIrradianceCoefficientCount; ++coefficientIndex)
                    {
                        coefficients[coefficientIndex] += radiance * basis[coefficientIndex] * solidAngle;
                    }

                    totalSolidAngle += solidAngle;
                }
            }
        }

        const float solidAngleCorrection = 4.0f * Pi / totalSolidAngle;
        constexpr std::array<float, 9> cosineConvolution = {
            Pi,
            2.0f * Pi / 3.0f,
            2.0f * Pi / 3.0f,
            2.0f * Pi / 3.0f,
            Pi / 4.0f,
            Pi / 4.0f,
            Pi / 4.0f,
            Pi / 4.0f,
            Pi / 4.0f
        };

        std::array<glm::vec4, DiffuseIrradianceCoefficientCount> result{};

        for (uint32_t coefficientIndex = 0; coefficientIndex < DiffuseIrradianceCoefficientCount; ++coefficientIndex)
        {
            const glm::vec3 irradianceCoefficient =
                coefficients[coefficientIndex] *
                solidAngleCorrection *
                cosineConvolution[coefficientIndex];

            result[coefficientIndex] = glm::vec4(irradianceCoefficient, 0.0f);
        }

        return result;
    }
}