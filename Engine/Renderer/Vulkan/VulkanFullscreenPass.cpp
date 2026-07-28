#include "Renderer/Vulkan/VulkanFullscreenPass.h"
#include "Renderer/Vulkan/VulkanDevice.h"
#include "Renderer/Vulkan/VulkanDescriptorSetLayout.h"
#include "Renderer/Vulkan/VulkanDescriptorPool.h"
#include "Renderer/Vulkan/VulkanDescriptorWriter.h"
#include "Renderer/Vulkan/VulkanGraphicsPipeline.h"
#include "Renderer/Vulkan/VulkanGraphicsPipelineDescription.h"

#include <filesystem>
#include <stdexcept>

namespace
{
    struct alignas(16) PostProcessPushConstant
    {
        float exposure = 1.0f;
        float padding[3] = {};
    };
}

namespace Kosmos
{
    VulkanFullscreenPass::VulkanFullscreenPass(VulkanDevice& device, VkRenderPass renderPass, VkExtent2D extent, const std::vector<VkImageView>& inputImageViews)
        : m_Device(device)
    {
        if (inputImageViews.empty())
        {
            throw std::runtime_error("Fullscreen pass requires at least one input image!");
        }

        for (VkImageView imageView : inputImageViews)
        {
            if (imageView == VK_NULL_HANDLE)
            {
                throw std::runtime_error("Fullscreen pass contains a null input image view!");
            }
        }

        VkDescriptorSetLayoutBinding inputBinding{};
        inputBinding.binding = 0;
        inputBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        inputBinding.descriptorCount = 1;
        inputBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        m_DescriptorSetLayout = std::make_unique<VulkanDescriptorSetLayout>(m_Device, std::vector<VkDescriptorSetLayoutBinding>{inputBinding});

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = static_cast<uint32_t>(inputImageViews.size());

        m_DescriptorPool = std::make_unique<VulkanDescriptorPool>(m_Device, static_cast<uint32_t>(inputImageViews.size()), std::vector<VkDescriptorPoolSize>{poolSize});
        m_DescriptorSets = m_DescriptorPool->AllocateSets(m_DescriptorSetLayout->GetHandle(), static_cast<uint32_t>(inputImageViews.size()));

        VulkanGraphicsPipelineDescription pipelineDescription{};
        pipelineDescription.vertexShaderPath = std::filesystem::path(KOSMOS_SHADER_DIR) / "Fullscreen.vert.spv";
        pipelineDescription.fragmentShaderPath = std::filesystem::path(KOSMOS_SHADER_DIR) / "Fullscreen.frag.spv";
        pipelineDescription.renderPass = renderPass;
        pipelineDescription.extent = extent;
        pipelineDescription.descriptorSetLayouts.push_back(m_DescriptorSetLayout->GetHandle());
        pipelineDescription.cullMode = VK_CULL_MODE_NONE;
        pipelineDescription.useDepthStencil = false;

        VkPushConstantRange postProcessRange{};
        postProcessRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        postProcessRange.offset = 0;
        postProcessRange.size = sizeof(PostProcessPushConstant);
        pipelineDescription.pushConstantRanges.push_back(postProcessRange);

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.blendEnable = VK_FALSE;
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        pipelineDescription.colorBlendAttachments.push_back(colorBlendAttachment);

        m_Pipeline = std::make_unique<VulkanGraphicsPipeline>(m_Device, pipelineDescription);

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        if (vkCreateSampler(m_Device.GetHandle(), &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create fullscreen sampler!");
        }

        VulkanDescriptorWriter writer(m_Device);

        for (size_t frameIndex = 0; frameIndex < inputImageViews.size(); ++frameIndex)
        {
            writer.WriteImage(m_DescriptorSets[frameIndex], 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, inputImageViews[frameIndex], m_Sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }

    VulkanFullscreenPass::~VulkanFullscreenPass()
    {
        if (m_Sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(m_Device.GetHandle(), m_Sampler, nullptr);
        }
    }

    void VulkanFullscreenPass::Record(VkCommandBuffer commandBuffer, uint32_t frameIndex, float exposure) const
    {
        const VkDescriptorSet descriptorSet = m_DescriptorSets.at(frameIndex);

        PostProcessPushConstant postProcess{};
        postProcess.exposure = exposure;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline->GetHandle());
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline->GetLayout(), 0, 1, &descriptorSet, 0, nullptr);
        vkCmdPushConstants(commandBuffer, m_Pipeline->GetLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PostProcessPushConstant), &postProcess);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    }
}