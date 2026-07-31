#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

namespace Kosmos
{
    class VulkanDevice;
    class VulkanImage;
    struct VulkanRenderTargetDescription;

    class VulkanRenderTarget
    {
        public:
            VulkanRenderTarget(VulkanDevice& device, const VulkanRenderTargetDescription& description);
            ~VulkanRenderTarget();

            VulkanRenderTarget(const VulkanRenderTarget&) = delete;
            VulkanRenderTarget& operator=(const VulkanRenderTarget&) = delete;

            VkRenderPass GetRenderPass() const { return m_RenderPass; }
            VkFramebuffer GetFramebuffer() const { return m_Framebuffer; }
            VkExtent2D GetExtent() const { return m_Extent; }

            size_t GetColorAttachmentCount() const { return m_ColorImages.size(); }
            const VulkanImage& GetColorImage(size_t index) const { return *m_ColorImages.at(index); }
            const VulkanImage* GetDepthImage() const { return m_DepthImage.get(); }

        private:
            void CreateImages(const VulkanRenderTargetDescription& description);
            void CreateRenderPass(const VulkanRenderTargetDescription& description);
            void CreateFramebuffer();
            VkFormat FindDepthFormat(VkFormatFeatureFlags requiredFeatures) const;
            bool SupportsFormat(VkFormat format, VkFormatFeatureFlags requiredFeatures) const;

        private:
            VulkanDevice& m_Device;
            VkExtent2D m_Extent{};

            std::vector<std::unique_ptr<VulkanImage>> m_ColorImages;
            std::unique_ptr<VulkanImage> m_DepthImage;

            VkRenderPass m_RenderPass = VK_NULL_HANDLE;
            VkFramebuffer m_Framebuffer = VK_NULL_HANDLE;
    };
}