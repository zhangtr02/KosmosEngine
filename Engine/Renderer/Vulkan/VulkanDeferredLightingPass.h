#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

namespace Kosmos
{
    class VulkanDevice;
    class VulkanGBuffer;
    class VulkanDescriptorSetLayout;
    class VulkanDescriptorPool;
    class VulkanGraphicsPipeline;

    class VulkanDeferredLightingPass
    {
        public:
            VulkanDeferredLightingPass(VulkanDevice& device, VkRenderPass renderPass, VkExtent2D extent, VkDescriptorSetLayout globalDescriptorSetLayout, const std::vector<const VulkanGBuffer*>& gBuffers, const std::vector<VkImageView>& ambientOcclusionImageViews);
            ~VulkanDeferredLightingPass();

            VulkanDeferredLightingPass(const VulkanDeferredLightingPass&) = delete;
            VulkanDeferredLightingPass& operator=(const VulkanDeferredLightingPass&) = delete;

            void Record(VkCommandBuffer commandBuffer, uint32_t frameIndex, VkDescriptorSet globalDescriptorSet) const;

        private:
            std::unique_ptr<VulkanDescriptorSetLayout> m_GBufferDescriptorSetLayout;
            std::unique_ptr<VulkanDescriptorPool> m_DescriptorPool;
            std::unique_ptr<VulkanGraphicsPipeline> m_Pipeline;
            std::vector<VkDescriptorSet> m_GBufferDescriptorSets;
    };
}