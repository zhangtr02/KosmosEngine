#include "Renderer/Vulkan/Pipelines/VulkanComputePipeline.h"
#include "Renderer/Vulkan/Core/VulkanDevice.h"

#include <fstream>
#include <stdexcept>
#include <vector>

namespace
{
    std::vector<char> ReadComputeShader(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::ate | std::ios::binary);

        if (!file.is_open())
        {
            throw std::runtime_error("Failed to open compute shader: " + path.string());
        }

        const std::streamsize size = file.tellg();

        if (size <= 0)
        {
            throw std::runtime_error("Compute shader is empty: " + path.string());
        }

        std::vector<char> code(static_cast<size_t>(size));
        file.seekg(0);
        file.read(code.data(), size);

        if (!file || code.size() % sizeof(uint32_t) != 0)
        {
            throw std::runtime_error("Failed to read valid compute shader: " + path.string());
        }

        return code;
    }
}

namespace Kosmos
{
    VulkanComputePipeline::VulkanComputePipeline(VulkanDevice& device, const std::filesystem::path& shaderPath, const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts, const std::vector<VkPushConstantRange>& pushConstantRanges)
        : m_Device(device)
    {
        const std::vector<char> code = ReadComputeShader(shaderPath);

        VkShaderModuleCreateInfo shaderInfo{};
        shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shaderInfo.codeSize = code.size();
        shaderInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

        if (vkCreateShaderModule(m_Device.GetHandle(), &shaderInfo, nullptr, &m_ShaderModule) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create compute shader module!");
        }

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
        layoutInfo.pSetLayouts = descriptorSetLayouts.empty() ? nullptr : descriptorSetLayouts.data();
        layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
        layoutInfo.pPushConstantRanges = pushConstantRanges.empty() ? nullptr : pushConstantRanges.data();

        if (vkCreatePipelineLayout(m_Device.GetHandle(), &layoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS)
        {
            vkDestroyShaderModule(m_Device.GetHandle(), m_ShaderModule, nullptr);
            m_ShaderModule = VK_NULL_HANDLE;
            throw std::runtime_error("Failed to create compute pipeline layout!");
        }

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageInfo.module = m_ShaderModule;
        stageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = stageInfo;
        pipelineInfo.layout = m_PipelineLayout;

        if (vkCreateComputePipelines(m_Device.GetHandle(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_Pipeline) != VK_SUCCESS)
        {
            vkDestroyPipelineLayout(m_Device.GetHandle(), m_PipelineLayout, nullptr);
            vkDestroyShaderModule(m_Device.GetHandle(), m_ShaderModule, nullptr);
            m_PipelineLayout = VK_NULL_HANDLE;
            m_ShaderModule = VK_NULL_HANDLE;
            throw std::runtime_error("Failed to create compute pipeline!");
        }
    }

    VulkanComputePipeline::~VulkanComputePipeline()
    {
        if (m_Pipeline != VK_NULL_HANDLE) vkDestroyPipeline(m_Device.GetHandle(), m_Pipeline, nullptr);
        if (m_PipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_Device.GetHandle(), m_PipelineLayout, nullptr);
        if (m_ShaderModule != VK_NULL_HANDLE) vkDestroyShaderModule(m_Device.GetHandle(), m_ShaderModule, nullptr);
    }
}