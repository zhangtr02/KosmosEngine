#pragma once

#include <vulkan/vulkan.h>
#include <memory>

namespace Kosmos
{
    class VulkanDevice;
    class VulkanRenderTarget;

    class VulkanGBuffer
    {
        public:
            static constexpr uint32_t ColorAttachmentCount = 3;

            VulkanGBuffer(VulkanDevice& device, VkExtent2D extent);
            ~VulkanGBuffer();

            VulkanGBuffer(const VulkanGBuffer&) = delete;
            VulkanGBuffer& operator=(const VulkanGBuffer&) = delete;

            VkRenderPass GetRenderPass() const;
            VkFramebuffer GetFramebuffer() const;
            VkExtent2D GetExtent() const;
            VkImageView GetAlbedoAmbientOcclusionImageView() const;
            VkImageView GetNormalRoughnessImageView() const;
            VkImageView GetMaterialParametersImageView() const;
            VkImageView GetDepthImageView() const;

        private:
            std::unique_ptr<VulkanRenderTarget> m_RenderTarget;
    };
}