#pragma once

#include <vulkan/vulkan.h>
#include <memory>

namespace Kosmos
{
    class VulkanDevice;
    class VulkanCubeTexture;
    class VulkanDescriptorSetLayout;
    class VulkanDescriptorPool;
    class VulkanGraphicsPipeline;

    class VulkanSkyboxPass
    {
        public:
            VulkanSkyboxPass(VulkanDevice& device, VkRenderPass renderPass, VkExtent2D extent, VkDescriptorSetLayout globalDescriptorSetLayout, const VulkanCubeTexture& environment);
            ~VulkanSkyboxPass() = default;

            VulkanSkyboxPass(const VulkanSkyboxPass&) = delete;
            VulkanSkyboxPass& operator=(const VulkanSkyboxPass&) = delete;

            void Record(VkCommandBuffer commandBuffer, VkDescriptorSet globalDescriptorSet) const;

        private:
            std::unique_ptr<VulkanDescriptorSetLayout> m_DescriptorSetLayout;
            std::unique_ptr<VulkanDescriptorPool> m_DescriptorPool;
            std::unique_ptr<VulkanGraphicsPipeline> m_Pipeline;
            VkDescriptorSet m_DescriptorSet = VK_NULL_HANDLE;
    };
}