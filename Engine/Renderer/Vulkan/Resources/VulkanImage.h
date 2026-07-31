#pragma once

#include <vulkan/vulkan.h>

namespace Kosmos
{
    class VulkanDevice;

    class VulkanImage
    {
        public:
        VulkanImage(
            VulkanDevice& device,
            uint32_t width,
            uint32_t height,
            VkFormat format,
            VkImageUsageFlags usage,
            VkImageAspectFlags aspectMask,
            uint32_t mipLevels = 1,
            uint32_t arrayLayers = 1,
            VkImageCreateFlags flags = 0,
            VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D);

            ~VulkanImage();

            VulkanImage(const VulkanImage&) = delete;
            VulkanImage& operator=(const VulkanImage&) = delete;

            VkImage GetHandle() const { return m_Image; }
            VkImageView GetImageView() const { return m_ImageView; }
            VkFormat GetFormat() const { return m_Format; }

        private:
            VulkanDevice& m_Device;

            VkImage m_Image = VK_NULL_HANDLE;
            VkDeviceMemory m_Memory = VK_NULL_HANDLE;
            VkImageView m_ImageView = VK_NULL_HANDLE;
            VkFormat m_Format = VK_FORMAT_UNDEFINED;
    };
}