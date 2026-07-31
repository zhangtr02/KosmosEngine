#pragma once

#include <vulkan/vulkan.h>
#include <memory>

namespace Kosmos
{
    class VulkanDevice;
    class VulkanGraphicsPipeline;

    class VulkanSkyboxPass
    {
        public:
            VulkanSkyboxPass(
                VulkanDevice& device,
                VkRenderPass renderPass,
                VkExtent2D extent,
                VkDescriptorSetLayout globalDescriptorSetLayout);

            ~VulkanSkyboxPass() = default;

            VulkanSkyboxPass(const VulkanSkyboxPass&) = delete;
            VulkanSkyboxPass& operator=(const VulkanSkyboxPass&) = delete;

            void Record(
                VkCommandBuffer commandBuffer,
                VkDescriptorSet globalDescriptorSet) const;

        private:
            std::unique_ptr<VulkanGraphicsPipeline> m_Pipeline;
    };
}