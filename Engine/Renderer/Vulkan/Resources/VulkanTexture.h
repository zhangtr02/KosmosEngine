#pragma once

#include <vulkan/vulkan.h>
#include <memory>

namespace Kosmos
{
    class Texture;
    class VulkanDevice;
    class VulkanImage;

    class VulkanTexture
    {
        public:
            VulkanTexture(VulkanDevice& device, const Texture& texture);
            ~VulkanTexture();

            VulkanTexture(const VulkanTexture&) = delete;
            VulkanTexture& operator=(const VulkanTexture&) = delete;

            VkImageView GetImageView() const;
            VkSampler GetSampler() const { return m_Sampler; }
            VkImageLayout GetLayout() const { return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; }

        private:
            VulkanDevice& m_Device;
            std::unique_ptr<VulkanImage> m_Image;
            VkSampler m_Sampler = VK_NULL_HANDLE;
    };
}