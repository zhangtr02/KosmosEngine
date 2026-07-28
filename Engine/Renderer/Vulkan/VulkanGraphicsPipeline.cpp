#include "Renderer/Vulkan/VulkanGraphicsPipeline.h"
#include "Renderer/Vulkan/VulkanGraphicsPipelineDescription.h"
#include "Renderer/Vulkan/VulkanDevice.h"

#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    std::vector<char> ReadFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::ate | std::ios::binary);

        if (!file.is_open())
        {
            throw std::runtime_error("Failed to open file: " + path.string());
        }

        const std::streamsize fileSize = file.tellg();

        if (fileSize <= 0)
        {
            throw std::runtime_error("Shader file is empty: " + path.string());
        }

        std::vector<char> data(static_cast<size_t>(fileSize));
        file.seekg(0);
        file.read(data.data(), fileSize);

        if (!file)
        {
            throw std::runtime_error("Failed to read file: " + path.string());
        }

        return data;
    }

    VkShaderModule CreateShaderModule(VkDevice device, const std::vector<char>& code)
    {
        if (code.empty() || code.size() % sizeof(uint32_t) != 0)
        {
            throw std::runtime_error("Invalid SPIR-V shader code!");
        }

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

        VkShaderModule shaderModule = VK_NULL_HANDLE;

        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create shader module!");
        }

        return shaderModule;
    }
}

namespace Kosmos
{
    VulkanGraphicsPipeline::VulkanGraphicsPipeline(VulkanDevice& device, const VulkanGraphicsPipelineDescription& description)
        : m_Device(device)
    {
        if (description.vertexShaderPath.empty())
        {
            throw std::runtime_error("Graphics pipeline requires a vertex shader!");
        }

        if (description.renderPass == VK_NULL_HANDLE)
        {
            throw std::runtime_error("Graphics pipeline requires a render pass!");
        }

        if (description.extent.width == 0 || description.extent.height == 0)
        {
            throw std::runtime_error("Graphics pipeline extent cannot be zero!");
        }

        for (VkDescriptorSetLayout descriptorSetLayout : description.descriptorSetLayouts)
        {
            if (descriptorSetLayout == VK_NULL_HANDLE)
            {
                throw std::runtime_error("Graphics pipeline contains a null descriptor set layout!");
            }
        }

        CreateShaderModules(description);
        CreatePipelineLayout(description);
        CreateGraphicsPipeline(description);
    }

    VulkanGraphicsPipeline::~VulkanGraphicsPipeline()
    {
        const VkDevice device = m_Device.GetHandle();

        if (m_Pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device, m_Pipeline, nullptr);
        }

        if (m_PipelineLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device, m_PipelineLayout, nullptr);
        }

        if (m_FragmentShaderModule != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(device, m_FragmentShaderModule, nullptr);
        }

        if (m_VertexShaderModule != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(device, m_VertexShaderModule, nullptr);
        }
    }

    void VulkanGraphicsPipeline::CreateShaderModules(const VulkanGraphicsPipelineDescription& description)
    {
        m_VertexShaderModule = CreateShaderModule(m_Device.GetHandle(), ReadFile(description.vertexShaderPath));

        if (!description.fragmentShaderPath.empty())
        {
            m_FragmentShaderModule = CreateShaderModule(m_Device.GetHandle(), ReadFile(description.fragmentShaderPath));
        }
    }

    void VulkanGraphicsPipeline::CreatePipelineLayout(const VulkanGraphicsPipelineDescription& description)
    {
        VkPipelineLayoutCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        createInfo.setLayoutCount = static_cast<uint32_t>(description.descriptorSetLayouts.size());
        createInfo.pSetLayouts = description.descriptorSetLayouts.empty() ? nullptr : description.descriptorSetLayouts.data();
        createInfo.pushConstantRangeCount = static_cast<uint32_t>(description.pushConstantRanges.size());
        createInfo.pPushConstantRanges = description.pushConstantRanges.empty() ? nullptr : description.pushConstantRanges.data();

        if (vkCreatePipelineLayout(m_Device.GetHandle(), &createInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create graphics pipeline layout!");
        }
    }

    void VulkanGraphicsPipeline::CreateGraphicsPipeline(const VulkanGraphicsPipelineDescription& description)
    {
        std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
        shaderStages.reserve(2);

        VkPipelineShaderStageCreateInfo vertexStage{};
        vertexStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertexStage.module = m_VertexShaderModule;
        vertexStage.pName = "main";
        shaderStages.push_back(vertexStage);

        if (m_FragmentShaderModule != VK_NULL_HANDLE)
        {
            VkPipelineShaderStageCreateInfo fragmentStage{};
            fragmentStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            fragmentStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            fragmentStage.module = m_FragmentShaderModule;
            fragmentStage.pName = "main";
            shaderStages.push_back(fragmentStage);
        }

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(description.vertexBindings.size());
        vertexInput.pVertexBindingDescriptions = description.vertexBindings.empty() ? nullptr : description.vertexBindings.data();
        vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(description.vertexAttributes.size());
        vertexInput.pVertexAttributeDescriptions = description.vertexAttributes.empty() ? nullptr : description.vertexAttributes.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = description.topology;
        inputAssembly.primitiveRestartEnable = description.primitiveRestartEnable;

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(description.extent.width);
        viewport.height = static_cast<float>(description.extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = description.extent;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterization{};
        rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterization.depthClampEnable = description.depthClampEnable;
        rasterization.rasterizerDiscardEnable = VK_FALSE;
        rasterization.polygonMode = description.polygonMode;
        rasterization.cullMode = description.cullMode;
        rasterization.frontFace = description.frontFace;
        rasterization.depthBiasEnable = description.depthBiasEnable;
        rasterization.depthBiasConstantFactor = description.depthBiasConstantFactor;
        rasterization.depthBiasClamp = description.depthBiasClamp;
        rasterization.depthBiasSlopeFactor = description.depthBiasSlopeFactor;
        rasterization.lineWidth = description.lineWidth;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = description.rasterizationSamples;
        multisampling.sampleShadingEnable = description.sampleShadingEnable;
        multisampling.minSampleShading = description.minSampleShading;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = description.depthTestEnable;
        depthStencil.depthWriteEnable = description.depthWriteEnable;
        depthStencil.depthCompareOp = description.depthCompareOp;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = static_cast<uint32_t>(description.colorBlendAttachments.size());
        colorBlending.pAttachments = description.colorBlendAttachments.empty() ? nullptr : description.colorBlendAttachments.data();

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
        pipelineInfo.pStages = shaderStages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterization;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = description.useDepthStencil ? &depthStencil : nullptr;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.layout = m_PipelineLayout;
        pipelineInfo.renderPass = description.renderPass;
        pipelineInfo.subpass = description.subpass;

        if (vkCreateGraphicsPipelines(m_Device.GetHandle(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_Pipeline) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create graphics pipeline!");
        }
    }
}