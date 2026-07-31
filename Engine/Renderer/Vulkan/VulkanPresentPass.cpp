#include "Renderer/Vulkan/VulkanPresentPass.h"
#include "Renderer/Vulkan/VulkanDevice.h"
#include "Renderer/Vulkan/VulkanDescriptorSetLayout.h"
#include "Renderer/Vulkan/VulkanDescriptorPool.h"
#include "Renderer/Vulkan/VulkanDescriptorWriter.h"
#include "Renderer/Vulkan/VulkanGraphicsPipeline.h"
#include "Renderer/Vulkan/VulkanGraphicsPipelineDescription.h"

#include <filesystem>
#include <stdexcept>
#include <vector>

namespace Kosmos
{
    VulkanPresentPass::VulkanPresentPass(VulkanDevice& device, VkRenderPass renderPass, VkExtent2D extent, const std::vector<VkImageView>& renderViewImageViews)
        : m_Device(device), m_RenderPass(renderPass), m_Extent(extent)
    {
        if (m_RenderPass == VK_NULL_HANDLE || m_Extent.width == 0 || m_Extent.height == 0 || renderViewImageViews.empty())
        {
            throw std::runtime_error("Present pass requires valid render resources!");
        }

        VkDescriptorSetLayoutBinding renderViewBinding{};
        renderViewBinding.binding = 0;
        renderViewBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        renderViewBinding.descriptorCount = 1;
        renderViewBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        m_DescriptorSetLayout = std::make_unique<VulkanDescriptorSetLayout>(m_Device, std::vector<VkDescriptorSetLayoutBinding>{renderViewBinding});

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = static_cast<uint32_t>(renderViewImageViews.size());
        m_DescriptorPool = std::make_unique<VulkanDescriptorPool>(m_Device, static_cast<uint32_t>(renderViewImageViews.size()), std::vector<VkDescriptorPoolSize>{poolSize});
        m_DescriptorSets = m_DescriptorPool->AllocateSets(m_DescriptorSetLayout->GetHandle(), static_cast<uint32_t>(renderViewImageViews.size()));

        VulkanGraphicsPipelineDescription pipelineDescription{};
        pipelineDescription.vertexShaderPath = std::filesystem::path(KOSMOS_SHADER_DIR) / "Fullscreen.vert.spv";
        pipelineDescription.fragmentShaderPath = std::filesystem::path(KOSMOS_SHADER_DIR) / "Present.frag.spv";
        pipelineDescription.renderPass = m_RenderPass;
        pipelineDescription.extent = m_Extent;
        pipelineDescription.descriptorSetLayouts.push_back(m_DescriptorSetLayout->GetHandle());
        pipelineDescription.cullMode = VK_CULL_MODE_NONE;
        pipelineDescription.useDepthStencil = false;

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
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;

        if (vkCreateSampler(m_Device.GetHandle(), &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create present sampler!");
        }

        VulkanDescriptorWriter writer(m_Device);

        for (uint32_t frameIndex = 0; frameIndex < static_cast<uint32_t>(renderViewImageViews.size()); ++frameIndex)
        {
            if (renderViewImageViews[frameIndex] == VK_NULL_HANDLE)
            {
                throw std::runtime_error("Present pass contains a null render-view image!");
            }

            writer.WriteImage(m_DescriptorSets[frameIndex], 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, renderViewImageViews[frameIndex], m_Sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }

    VulkanPresentPass::~VulkanPresentPass()
    {
        if (m_Sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(m_Device.GetHandle(), m_Sampler, nullptr);
        }
    }

    void VulkanPresentPass::Record(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, uint32_t frameIndex) const
    {
        if (framebuffer == VK_NULL_HANDLE)
        {
            throw std::runtime_error("Present pass requires a framebuffer!");
        }

        VkClearValue clearValue{};
        clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = m_RenderPass;
        renderPassInfo.framebuffer = framebuffer;
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = m_Extent;
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearValue;

        const VkDescriptorSet descriptorSet = m_DescriptorSets.at(frameIndex);
        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline->GetHandle());
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline->GetLayout(), 0, 1, &descriptorSet, 0, nullptr);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        vkCmdEndRenderPass(commandBuffer);
    }
}
