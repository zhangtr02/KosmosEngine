#pragma once

#include <vulkan/vulkan.h>
#include <memory>

namespace Kosmos
{
    class CubeTexture;
    class VulkanDevice;
    class VulkanImage;

    class VulkanCubeTexture
    {
        public:
            VulkanCubeTexture(VulkanDevice& device, const CubeTexture& texture);
            ~VulkanCubeTexture();

            VulkanCubeTexture(const VulkanCubeTexture&) = delete;
            VulkanCubeTexture& operator=(const VulkanCubeTexture&) = delete;

            VkImageView GetImageView() const;
            VkSampler GetSampler() const { return m_Sampler; }
            VkImageLayout GetLayout() const { return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; }

        private:
            VulkanDevice& m_Device;
            std::unique_ptr<VulkanImage> m_Image;
            VkSampler m_Sampler = VK_NULL_HANDLE;
    };
}