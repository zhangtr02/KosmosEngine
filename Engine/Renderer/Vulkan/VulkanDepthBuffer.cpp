#include "Renderer/Vulkan/VulkanDepthBuffer.h"
#include "Renderer/Vulkan/VulkanDevice.h"

#include <array>
#include <stdexcept>

namespace Kosmos
{
    VulkanDepthBuffer::VulkanDepthBuffer(VulkanDevice& device, VkExtent2D extent)
        : m_Device(device)
    {
        if (extent.width == 0 || extent.height == 0)
        {
            throw std::runtime_error("Cannot create a depth buffer with zero extent!");
        }

        m_Format = FindDepthFormat();

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = extent.width;
        imageInfo.extent.height = extent.height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = m_Format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(m_Device.GetHandle(), &imageInfo, nullptr, &m_Image) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create depth image!");
        }

        VkMemoryRequirements memoryRequirements{};
        vkGetImageMemoryRequirements(m_Device.GetHandle(), m_Image, &memoryRequirements);

        VkMemoryAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = memoryRequirements.size;
        allocateInfo.memoryTypeIndex = m_Device.FindMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(m_Device.GetHandle(), &allocateInfo, nullptr, &m_Memory) != VK_SUCCESS)
        {
            vkDestroyImage(m_Device.GetHandle(), m_Image, nullptr);
            m_Image = VK_NULL_HANDLE;
            throw std::runtime_error("Failed to allocate depth image memory!");
        }

        if (vkBindImageMemory(m_Device.GetHandle(), m_Image, m_Memory, 0) != VK_SUCCESS)
        {
            vkFreeMemory(m_Device.GetHandle(), m_Memory, nullptr);
            vkDestroyImage(m_Device.GetHandle(), m_Image, nullptr);
            m_Memory = VK_NULL_HANDLE;
            m_Image = VK_NULL_HANDLE;
            throw std::runtime_error("Failed to bind depth image memory!");
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_Image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_Format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_Device.GetHandle(), &viewInfo, nullptr, &m_ImageView) != VK_SUCCESS)
        {
            vkDestroyImage(m_Device.GetHandle(), m_Image, nullptr);
            vkFreeMemory(m_Device.GetHandle(), m_Memory, nullptr);
            m_Image = VK_NULL_HANDLE;
            m_Memory = VK_NULL_HANDLE;
            throw std::runtime_error("Failed to create depth image view!");
        }
    }

    VulkanDepthBuffer::~VulkanDepthBuffer()
    {
        if (m_ImageView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(m_Device.GetHandle(), m_ImageView, nullptr);
        }

        if (m_Image != VK_NULL_HANDLE)
        {
            vkDestroyImage(m_Device.GetHandle(), m_Image, nullptr);
        }

        if (m_Memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(m_Device.GetHandle(), m_Memory, nullptr);
        }
    }

    VkFormat VulkanDepthBuffer::FindDepthFormat() const
    {
        constexpr std::array<VkFormat, 3> candidates = {
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D24_UNORM_S8_UINT
        };

        for (VkFormat format : candidates)
        {
            VkFormatProperties properties{};
            vkGetPhysicalDeviceFormatProperties(m_Device.GetPhysicalDevice(), format, &properties);

            if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
            {
                return format;
            }
        }

        throw std::runtime_error("Failed to find a supported depth format!");
    }
}