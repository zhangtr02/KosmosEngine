#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

namespace Kosmos
{
    class VulkanDevice;
    class VulkanGBuffer;
    class VulkanRenderTarget;
    class VulkanDescriptorSetLayout;
    class VulkanDescriptorPool;
    class VulkanGraphicsPipeline;

    class VulkanSSAOPass
    {
        public:
            VulkanSSAOPass(VulkanDevice& device, VkExtent2D extent, VkDescriptorSetLayout globalDescriptorSetLayout, const std::vector<const VulkanGBuffer*>& gBuffers);
            ~VulkanSSAOPass();

            VulkanSSAOPass(const VulkanSSAOPass&) = delete;
            VulkanSSAOPass& operator=(const VulkanSSAOPass&) = delete;

            void Record(VkCommandBuffer commandBuffer, uint32_t frameIndex, VkDescriptorSet globalDescriptorSet, float radius, float bias, float power) const;
            VkImageView GetAmbientOcclusionImageView(uint32_t frameIndex) const;

        private:
            VulkanDevice& m_Device;
            std::vector<std::unique_ptr<VulkanRenderTarget>> m_RenderTargets;
            std::unique_ptr<VulkanDescriptorSetLayout> m_GBufferDescriptorSetLayout;
            std::unique_ptr<VulkanDescriptorPool> m_DescriptorPool;
            std::unique_ptr<VulkanGraphicsPipeline> m_Pipeline;
            std::vector<VkDescriptorSet> m_GBufferDescriptorSets;
    };
}