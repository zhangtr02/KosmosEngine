#pragma once

#include <vulkan/vulkan.h>

namespace Kosmos
{
    class VulkanDevice;
    struct VulkanGraphicsPipelineDescription;

    class VulkanGraphicsPipeline
    {
        public:
            VulkanGraphicsPipeline(VulkanDevice& device, const VulkanGraphicsPipelineDescription& description);
            ~VulkanGraphicsPipeline();

            VulkanGraphicsPipeline(const VulkanGraphicsPipeline&) = delete;
            VulkanGraphicsPipeline& operator=(const VulkanGraphicsPipeline&) = delete;

            VkPipeline GetHandle() const { return m_Pipeline; }
            VkPipelineLayout GetLayout() const { return m_PipelineLayout; }

        private:
            void CreateShaderModules(const VulkanGraphicsPipelineDescription& description);
            void CreatePipelineLayout(const VulkanGraphicsPipelineDescription& description);
            void CreateGraphicsPipeline(const VulkanGraphicsPipelineDescription& description);

        private:
            VulkanDevice& m_Device;
            VkShaderModule m_VertexShaderModule = VK_NULL_HANDLE;
            VkShaderModule m_FragmentShaderModule = VK_NULL_HANDLE;
            VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
            VkPipeline m_Pipeline = VK_NULL_HANDLE;
    };
}