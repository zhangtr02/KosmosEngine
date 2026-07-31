#include "Renderer/Lighting/BrdfLutGenerator.h"
#include "Renderer/Resources/Texture.h"

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace
{
    constexpr float Pi = 3.14159265359f;

    float RadicalInverseVanDerCorput(uint32_t bits)
    {
        bits = (bits << 16u) | (bits >> 16u);
        bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
        bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
        bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
        bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
        return static_cast<float>(bits) * 2.3283064365386963e-10f;
    }

    glm::vec2 Hammersley(uint32_t sampleIndex, uint32_t sampleCount)
    {
        return {
            static_cast<float>(sampleIndex) / static_cast<float>(sampleCount),
            RadicalInverseVanDerCorput(sampleIndex)
        };
    }

    glm::vec3 ImportanceSampleGGX(const glm::vec2& sample, float roughness)
    {
        const float alpha = roughness * roughness;
        const float alphaSquared = alpha * alpha;
        const float phi = 2.0f * Pi * sample.x;
        const float cosineTheta = std::sqrt((1.0f - sample.y) / (1.0f + (alphaSquared - 1.0f) * sample.y));
        const float sineTheta = std::sqrt(std::max(1.0f - cosineTheta * cosineTheta, 0.0f));

        return glm::normalize(glm::vec3(
            std::cos(phi) * sineTheta,
            std::sin(phi) * sineTheta,
            cosineTheta));
    }

    float GeometrySchlickGGX(float normalDotDirection, float roughness)
    {
        const float k = roughness * roughness * 0.5f;
        return normalDotDirection / std::max(normalDotDirection * (1.0f - k) + k, 0.000001f);
    }

    float GeometrySmith(float normalDotView, float normalDotLight, float roughness)
    {
        return GeometrySchlickGGX(normalDotView, roughness) * GeometrySchlickGGX(normalDotLight, roughness);
    }

    glm::vec2 IntegrateBrdf(float normalDotView, float roughness, uint32_t sampleCount)
    {
        const glm::vec3 normal(0.0f, 0.0f, 1.0f);
        const glm::vec3 viewDirection(
            std::sqrt(std::max(1.0f - normalDotView * normalDotView, 0.0f)),
            0.0f,
            normalDotView);

        float scale = 0.0f;
        float bias = 0.0f;

        for (uint32_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
        {
            const glm::vec2 sample = Hammersley(sampleIndex, sampleCount);
            const glm::vec3 halfDirection = ImportanceSampleGGX(sample, roughness);
            const glm::vec3 lightDirection = glm::normalize(2.0f * glm::dot(viewDirection, halfDirection) * halfDirection - viewDirection);

            const float normalDotLight = std::max(lightDirection.z, 0.0f);
            const float normalDotHalf = std::max(halfDirection.z, 0.0f);
            const float viewDotHalf = std::max(glm::dot(viewDirection, halfDirection), 0.0f);

            if (normalDotLight <= 0.0f)
            {
                continue;
            }

            const float geometry = GeometrySmith(normalDotView, normalDotLight, roughness);
            const float visibility = geometry * viewDotHalf / std::max(normalDotHalf * normalDotView, 0.000001f);
            const float fresnel = std::pow(1.0f - viewDotHalf, 5.0f);

            scale += (1.0f - fresnel) * visibility;
            bias += fresnel * visibility;
        }

        return glm::vec2(scale, bias) / static_cast<float>(sampleCount);
    }
}

namespace Kosmos
{
    std::shared_ptr<Texture> BrdfLutGenerator::Generate(uint32_t resolution, uint32_t sampleCount)
    {
        if (resolution == 0 || sampleCount == 0)
        {
            throw std::runtime_error("BRDF LUT resolution and sample count must be greater than zero!");
        }

        std::vector<uint8_t> pixels(static_cast<size_t>(resolution) * resolution * 4);

        for (uint32_t y = 0; y < resolution; ++y)
        {
            const float roughness = (static_cast<float>(y) + 0.5f) / static_cast<float>(resolution);

            for (uint32_t x = 0; x < resolution; ++x)
            {
                const float normalDotView = (static_cast<float>(x) + 0.5f) / static_cast<float>(resolution);
                const glm::vec2 integratedBrdf = IntegrateBrdf(normalDotView, roughness, sampleCount);
                const size_t pixelIndex = (static_cast<size_t>(y) * resolution + x) * 4;

                pixels[pixelIndex] = static_cast<uint8_t>(std::clamp(integratedBrdf.x, 0.0f, 1.0f) * 255.0f + 0.5f);
                pixels[pixelIndex + 1] = static_cast<uint8_t>(std::clamp(integratedBrdf.y, 0.0f, 1.0f) * 255.0f + 0.5f);
                pixels[pixelIndex + 2] = 0;
                pixels[pixelIndex + 3] = 255;
            }
        }

        return std::make_shared<Texture>(resolution, resolution, std::move(pixels), TextureColorSpace::Linear);
    }
}