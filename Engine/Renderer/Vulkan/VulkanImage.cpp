#include "Renderer/Vulkan/VulkanImage.h"
#include "Renderer/Vulkan/VulkanDevice.h"

#include <stdexcept>

namespace Kosmos
{
    VulkanImage::VulkanImage(
        VulkanDevice& device,
        uint32_t width,
        uint32_t height,
        VkFormat format,
        VkImageUsageFlags usage,
        VkImageAspectFlags aspectMask,
        uint32_t mipLevels,
        uint32_t arrayLayers,
        VkImageCreateFlags flags,
        VkImageViewType viewType)
        : m_Device(device), m_Format(format)
    {
        if (width == 0 || height == 0)
        {
            throw std::runtime_error("Cannot create a Vulkan image with zero extent!");
        }

        if (mipLevels == 0)
        {
            throw std::runtime_error("Cannot create a Vulkan image without mip levels!");
        }

        if (arrayLayers == 0)
        {
            throw std::runtime_error("Cannot create a Vulkan image without array layers!");
        }

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {width, height, 1};
        imageInfo.mipLevels = mipLevels;
        imageInfo.flags = flags;
        imageInfo.arrayLayers = arrayLayers;
        imageInfo.format = m_Format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = usage;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(m_Device.GetHandle(), &imageInfo, nullptr, &m_Image) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create Vulkan image!");
        }

        VkMemoryRequirements memoryRequirements{};
        vkGetImageMemoryRequirements(m_Device.GetHandle(), m_Image, &memoryRequirements);

        uint32_t memoryTypeIndex = 0;

        if (!m_Device.TryFindMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memoryTypeIndex))
        {
            vkDestroyImage(m_Device.GetHandle(), m_Image, nullptr);
            m_Image = VK_NULL_HANDLE;
            throw std::runtime_error("Failed to find memory for Vulkan image!");
        }

        VkMemoryAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = memoryRequirements.size;
        allocateInfo.memoryTypeIndex = memoryTypeIndex;

        if (vkAllocateMemory(m_Device.GetHandle(), &allocateInfo, nullptr, &m_Memory) != VK_SUCCESS)
        {
            vkDestroyImage(m_Device.GetHandle(), m_Image, nullptr);
            m_Image = VK_NULL_HANDLE;
            throw std::runtime_error("Failed to allocate Vulkan image memory!");
        }

        if (vkBindImageMemory(m_Device.GetHandle(), m_Image, m_Memory, 0) != VK_SUCCESS)
        {
            vkFreeMemory(m_Device.GetHandle(), m_Memory, nullptr);
            vkDestroyImage(m_Device.GetHandle(), m_Image, nullptr);
            m_Memory = VK_NULL_HANDLE;
            m_Image = VK_NULL_HANDLE;
            throw std::runtime_error("Failed to bind Vulkan image memory!");
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_Image;
        viewInfo.viewType = viewType;
        viewInfo.format = m_Format;
        viewInfo.subresourceRange.aspectMask = aspectMask;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = mipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = arrayLayers;

        if (vkCreateImageView(m_Device.GetHandle(), &viewInfo, nullptr, &m_ImageView) != VK_SUCCESS)
        {
            vkDestroyImage(m_Device.GetHandle(), m_Image, nullptr);
            vkFreeMemory(m_Device.GetHandle(), m_Memory, nullptr);
            m_Image = VK_NULL_HANDLE;
            m_Memory = VK_NULL_HANDLE;
            throw std::runtime_error("Failed to create Vulkan image view!");
        }
    }

    VulkanImage::~VulkanImage()
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
}