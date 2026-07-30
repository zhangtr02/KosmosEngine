#pragma once

#include <vulkan/vulkan.h>
#include <filesystem>
#include <vector>

namespace Kosmos
{
    class VulkanDevice;

    class VulkanComputePipeline
    {
        public:
            VulkanComputePipeline(VulkanDevice& device, const std::filesystem::path& shaderPath, const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts, const std::vector<VkPushConstantRange>& pushConstantRanges = {});
            ~VulkanComputePipeline();

            VulkanComputePipeline(const VulkanComputePipeline&) = delete;
            VulkanComputePipeline& operator=(const VulkanComputePipeline&) = delete;

            VkPipeline GetHandle() const { return m_Pipeline; }
            VkPipelineLayout GetLayout() const { return m_PipelineLayout; }

        private:
            VulkanDevice& m_Device;
            VkShaderModule m_ShaderModule = VK_NULL_HANDLE;
            VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
            VkPipeline m_Pipeline = VK_NULL_HANDLE;
    };
}