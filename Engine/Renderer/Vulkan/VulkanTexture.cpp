#include "Renderer/Vulkan/VulkanTexture.h"
#include "Renderer/Vulkan/VulkanImage.h"
#include "Renderer/Vulkan/VulkanBuffer.h"
#include "Renderer/Vulkan/VulkanDevice.h"
#include "Renderer/Texture.h"

#include <stdexcept>
#include <algorithm>
#include <cmath>

namespace Kosmos
{
    VulkanTexture::VulkanTexture(VulkanDevice& device, const Texture& texture)
        : m_Device(device)
    {
        const VkDeviceSize imageSize = static_cast<VkDeviceSize>(texture.GetPixels().size());
        const uint32_t mipLevels = static_cast<uint32_t>(
            std::floor(std::log2(std::max(texture.GetWidth(), texture.GetHeight())))) + 1;

        VulkanBuffer stagingBuffer(
            m_Device,
            imageSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        stagingBuffer.Write(texture.GetPixels().data(), imageSize);

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
            mipLevels);

        m_Device.TransitionImageLayout(
            m_Image->GetHandle(),
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            mipLevels);

        m_Device.CopyBufferToImage(
            stagingBuffer,
            m_Image->GetHandle(),
            texture.GetWidth(),
            texture.GetHeight());

        m_Device.GenerateMipmaps(
            m_Image->GetHandle(),
            format,
            texture.GetWidth(),
            texture.GetHeight(),
            mipLevels);

        VkPhysicalDeviceProperties deviceProperties{};
        vkGetPhysicalDeviceProperties(m_Device.GetPhysicalDevice(), &deviceProperties);

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = deviceProperties.limits.maxSamplerAnisotropy;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = static_cast<float>(mipLevels - 1);
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        if (vkCreateSampler(m_Device.GetHandle(), &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create Vulkan texture sampler!");
        }
    }

    VulkanTexture::~VulkanTexture()
    {
        if (m_Sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(m_Device.GetHandle(), m_Sampler, nullptr);
        }
    }

    VkImageView VulkanTexture::GetImageView() const
    {
        return m_Image->GetImageView();
    }
}