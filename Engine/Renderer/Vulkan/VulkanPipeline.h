#pragma once

#include <vulkan/vulkan.h>

namespace Kosmos
{
    class VulkanDevice;

    class VulkanPipeline
    {
        public:
            VulkanPipeline(VulkanDevice& device, VkRenderPass renderPass, VkExtent2D extent);
            ~VulkanPipeline();

            VulkanPipeline(const VulkanPipeline&) = delete;
            VulkanPipeline& operator=(const VulkanPipeline&) = delete;

            VkPipeline GetHandle() const { return m_Pipeline; }
            VkPipelineLayout GetLayout() const { return m_PipelineLayout; }

        private:
            void CreateShaderModules();
            void CreatePipelineLayout();
            void CreateGraphicsPipeline(VkRenderPass renderPass, VkExtent2D extent);

        private:
            VulkanDevice& m_Device;

            VkShaderModule m_VertexShaderModule = VK_NULL_HANDLE;
            VkShaderModule m_FragmentShaderModule = VK_NULL_HANDLE;

            VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
            VkPipeline m_Pipeline = VK_NULL_HANDLE;
    };
}