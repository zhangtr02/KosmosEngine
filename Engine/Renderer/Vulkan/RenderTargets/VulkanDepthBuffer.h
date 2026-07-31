 #pragma once

#include <vulkan/vulkan.h>

namespace Kosmos
{
    class VulkanDevice;

    class VulkanDepthBuffer
    {
        public:
            VulkanDepthBuffer(VulkanDevice& device, VkExtent2D extent);
            ~VulkanDepthBuffer();

            VulkanDepthBuffer(const VulkanDepthBuffer&) = delete;
            VulkanDepthBuffer& operator=(const VulkanDepthBuffer&) = delete;

            VkImageView GetImageView() const { return m_ImageView; }
            VkFormat GetFormat() const { return m_Format; }

        private:
            VkFormat FindDepthFormat() const;

        private:
            VulkanDevice& m_Device;

            VkImage m_Image = VK_NULL_HANDLE;
            VkDeviceMemory m_Memory = VK_NULL_HANDLE;
            VkImageView m_ImageView = VK_NULL_HANDLE;
            VkFormat m_Format = VK_FORMAT_UNDEFINED;
    };
}