#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

namespace Kosmos
{
    class VulkanDevice;
    class VulkanDescriptorSetLayout;
    class VulkanDescriptorPool;
    class VulkanGraphicsPipeline;

    class VulkanPresentPass
    {
        public:
            VulkanPresentPass(VulkanDevice& device, VkRenderPass renderPass, VkExtent2D extent, const std::vector<VkImageView>& renderViewImageViews);
            ~VulkanPresentPass();

            VulkanPresentPass(const VulkanPresentPass&) = delete;
            VulkanPresentPass& operator=(const VulkanPresentPass&) = delete;

            void Record(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, uint32_t frameIndex) const;

        private:
            VulkanDevice& m_Device;
            VkRenderPass m_RenderPass = VK_NULL_HANDLE;
            VkExtent2D m_Extent{};
            std::unique_ptr<VulkanDescriptorSetLayout> m_DescriptorSetLayout;
            std::unique_ptr<VulkanDescriptorPool> m_DescriptorPool;
            std::unique_ptr<VulkanGraphicsPipeline> m_Pipeline;
            std::vector<VkDescriptorSet> m_DescriptorSets;
            VkSampler m_Sampler = VK_NULL_HANDLE;
    };
}
