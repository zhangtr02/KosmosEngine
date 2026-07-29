#include "Renderer/Vulkan/VulkanCubeTexture.h"
#include "Renderer/Vulkan/VulkanImage.h"
#include "Renderer/Vulkan/VulkanBuffer.h"
#include "Renderer/Vulkan/VulkanDevice.h"
#include "Renderer/CubeTexture.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace Kosmos
{
    VulkanCubeTexture::VulkanCubeTexture(VulkanDevice& device, const CubeTexture& texture)
        : m_Device(device)
    {
        const uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texture.GetWidth(), texture.GetHeight())))) + 1;
        const size_t faceSize = texture.GetFaces()[0]->GetPixels().size();

        std::vector<uint8_t> pixels;
        pixels.reserve(faceSize * CubeTexture::FaceCount);

        for (const std::shared_ptr<Texture>& face : texture.GetFaces())
        {
            pixels.insert(pixels.end(), face->GetPixels().begin(), face->GetPixels().end());
        }

        VulkanBuffer stagingBuffer(
            m_Device,
            static_cast<VkDeviceSize>(pixels.size()),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        stagingBuffer.Write(pixels.data(), static_cast<VkDeviceSize>(pixels.size()));

        const VkFormat format = texture.GetColorSpace() == TextureColorSpace::SRGB ?
            VK_FORMAT_R8G8B8A8_SRGB :
            VK_FORMAT_R8G8B8A8_UNORM;

        m_Image = std::make_unique<VulkanImage>(
            m_Device,
            texture.GetWidth(),
            texture.GetHeight(),
            format,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            mipLevels,
            CubeTexture::FaceCount,
            VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
            VK_IMAGE_VIEW_TYPE_CUBE);

        m_Device.TransitionImageLayout(
            m_Image->GetHandle(),
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            mipLevels,
            CubeTexture::FaceCount);

        m_Device.CopyBufferToImage(
            stagingBuffer,
            m_Image->GetHandle(),
            texture.GetWidth(),
            texture.GetHeight(),
            CubeTexture::FaceCount);

        m_Device.GenerateMipmaps(
            m_Image->GetHandle(),
            format,
            texture.GetWidth(),
            texture.GetHeight(),
            mipLevels,
            CubeTexture::FaceCount);

        VkPhysicalDeviceProperties deviceProperties{};
        vkGetPhysicalDeviceProperties(m_Device.GetPhysicalDevice(), &deviceProperties);

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = deviceProperties.limits.maxSamplerAnisotropy;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = static_cast<float>(mipLevels - 1);
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        if (vkCreateSampler(m_Device.GetHandle(), &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create Vulkan cube texture sampler!");
        }
    }

    VulkanCubeTexture::~VulkanCubeTexture()
    {
        if (m_Sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(m_Device.GetHandle(), m_Sampler, nullptr);
        }
    }

    VkImageView VulkanCubeTexture::GetImageView() const
    {
        return m_Image->GetImageView();
    }
}