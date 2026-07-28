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

    class VulkanFullscreenPass
    {
        public:
            VulkanFullscreenPass(VulkanDevice& device, VkRenderPass renderPass, VkExtent2D extent, const std::vector<VkImageView>& inputImageViews);
            ~VulkanFullscreenPass();

            VulkanFullscreenPass(const VulkanFullscreenPass&) = delete;
            VulkanFullscreenPass& operator=(const VulkanFullscreenPass&) = delete;

            void Record(VkCommandBuffer commandBuffer, uint32_t frameIndex) const;

        private:
            VulkanDevice& m_Device;
            std::unique_ptr<VulkanDescriptorSetLayout> m_DescriptorSetLayout;
            std::unique_ptr<VulkanDescriptorPool> m_DescriptorPool;
            std::unique_ptr<VulkanGraphicsPipeline> m_Pipeline;
            std::vector<VkDescriptorSet> m_DescriptorSets;
            VkSampler m_Sampler = VK_NULL_HANDLE;
    };
}