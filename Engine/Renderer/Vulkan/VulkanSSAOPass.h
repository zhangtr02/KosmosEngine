#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
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

            void Record(VkCommandBuffer commandBuffer, uint32_t frameIndex, VkDescriptorSet globalDescriptorSet, float radius, float bias, float power, float depthSharpness, float normalSharpness) const;
            VkImageView GetAmbientOcclusionImageView(uint32_t frameIndex) const;

        private:
            void RecordBlurPass(VkCommandBuffer commandBuffer, const VulkanRenderTarget& renderTarget, VkDescriptorSet descriptorSet, VkDescriptorSet globalDescriptorSet, int32_t directionX, int32_t directionY, float depthSharpness, float normalSharpness) const;

        private:
            VulkanDevice& m_Device;
            std::vector<std::unique_ptr<VulkanRenderTarget>> m_RawRenderTargets;
            std::vector<std::unique_ptr<VulkanRenderTarget>> m_BlurIntermediateRenderTargets;
            std::vector<std::unique_ptr<VulkanRenderTarget>> m_BlurredRenderTargets;
            std::unique_ptr<VulkanDescriptorSetLayout> m_GBufferDescriptorSetLayout;
            std::unique_ptr<VulkanDescriptorSetLayout> m_BlurDescriptorSetLayout;
            std::unique_ptr<VulkanDescriptorPool> m_DescriptorPool;
            std::unique_ptr<VulkanGraphicsPipeline> m_RawPipeline;
            std::unique_ptr<VulkanGraphicsPipeline> m_BlurPipeline;
            std::vector<VkDescriptorSet> m_GBufferDescriptorSets;
            std::vector<VkDescriptorSet> m_HorizontalBlurDescriptorSets;
            std::vector<VkDescriptorSet> m_VerticalBlurDescriptorSets;
    };
}