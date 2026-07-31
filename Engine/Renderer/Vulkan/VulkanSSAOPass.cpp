#include "Renderer/Vulkan/VulkanSSAOPass.h"
#include "Renderer/Vulkan/VulkanDevice.h"
#include "Renderer/Vulkan/VulkanGBuffer.h"
#include "Renderer/Vulkan/VulkanRenderTarget.h"
#include "Renderer/Vulkan/VulkanRenderTargetDescription.h"
#include "Renderer/Vulkan/VulkanImage.h"
#include "Renderer/Vulkan/VulkanDescriptorSetLayout.h"
#include "Renderer/Vulkan/VulkanDescriptorPool.h"
#include "Renderer/Vulkan/VulkanDescriptorWriter.h"
#include "Renderer/Vulkan/VulkanGraphicsPipeline.h"
#include "Renderer/Vulkan/VulkanGraphicsPipelineDescription.h"

#include <filesystem>
#include <stdexcept>
#include <vector>

namespace
{
    struct SSAOPushConstant
    {
        float radius = 0.75f;
        float bias = 0.025f;
        float power = 1.5f;
    };

    static_assert(sizeof(SSAOPushConstant) == 12);
}

namespace Kosmos
{
    VulkanSSAOPass::VulkanSSAOPass(VulkanDevice& device, VkExtent2D extent, VkDescriptorSetLayout globalDescriptorSetLayout, const std::vector<const VulkanGBuffer*>& gBuffers)
        : m_Device(device)
    {
        if (extent.width == 0 || extent.height == 0 || globalDescriptorSetLayout == VK_NULL_HANDLE || gBuffers.empty())
        {
            throw std::runtime_error("SSAO pass requires valid render resources!");
        }

        VulkanRenderTargetDescription renderTargetDescription{};
        renderTargetDescription.extent = extent;

        VulkanRenderTargetColorAttachmentDescription colorAttachment{};
        colorAttachment.format = VK_FORMAT_R8_UNORM;
        colorAttachment.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        renderTargetDescription.colorAttachments.push_back(colorAttachment);

        m_RenderTargets.reserve(gBuffers.size());

        for (const VulkanGBuffer* gBuffer : gBuffers)
        {
            if (!gBuffer)
            {
                throw std::runtime_error("SSAO pass contains a null G-buffer!");
            }

            m_RenderTargets.push_back(std::make_unique<VulkanRenderTarget>(m_Device, renderTargetDescription));
        }

        std::vector<VkDescriptorSetLayoutBinding> bindings(2);

        for (uint32_t bindingIndex = 0; bindingIndex < static_cast<uint32_t>(bindings.size()); ++bindingIndex)
        {
            bindings[bindingIndex].binding = bindingIndex;
            bindings[bindingIndex].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            bindings[bindingIndex].descriptorCount = 1;
            bindings[bindingIndex].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }

        m_GBufferDescriptorSetLayout = std::make_unique<VulkanDescriptorSetLayout>(m_Device, bindings);

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        poolSize.descriptorCount = static_cast<uint32_t>(gBuffers.size()) * 2;

        m_DescriptorPool = std::make_unique<VulkanDescriptorPool>(m_Device, static_cast<uint32_t>(gBuffers.size()), std::vector<VkDescriptorPoolSize>{poolSize});
        m_GBufferDescriptorSets = m_DescriptorPool->AllocateSets(m_GBufferDescriptorSetLayout->GetHandle(), static_cast<uint32_t>(gBuffers.size()));

        VulkanDescriptorWriter writer(m_Device);

        for (uint32_t frameIndex = 0; frameIndex < static_cast<uint32_t>(gBuffers.size()); ++frameIndex)
        {
            writer.WriteImage(m_GBufferDescriptorSets[frameIndex], 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, gBuffers[frameIndex]->GetNormalRoughnessImageView(), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            writer.WriteImage(m_GBufferDescriptorSets[frameIndex], 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, gBuffers[frameIndex]->GetDepthImageView(), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        }

        VulkanGraphicsPipelineDescription pipelineDescription{};
        pipelineDescription.vertexShaderPath = std::filesystem::path(KOSMOS_SHADER_DIR) / "Fullscreen.vert.spv";
        pipelineDescription.fragmentShaderPath = std::filesystem::path(KOSMOS_SHADER_DIR) / "SSAO.frag.spv";
        pipelineDescription.renderPass = m_RenderTargets.front()->GetRenderPass();
        pipelineDescription.extent = extent;
        pipelineDescription.descriptorSetLayouts = {globalDescriptorSetLayout, m_GBufferDescriptorSetLayout->GetHandle()};
        pipelineDescription.cullMode = VK_CULL_MODE_NONE;
        pipelineDescription.useDepthStencil = false;

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(SSAOPushConstant);
        pipelineDescription.pushConstantRanges.push_back(pushConstantRange);

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.blendEnable = VK_FALSE;
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
        pipelineDescription.colorBlendAttachments.push_back(colorBlendAttachment);

        m_Pipeline = std::make_unique<VulkanGraphicsPipeline>(m_Device, pipelineDescription);
    }

    VulkanSSAOPass::~VulkanSSAOPass() = default;

    VkImageView VulkanSSAOPass::GetAmbientOcclusionImageView(uint32_t frameIndex) const
    {
        return m_RenderTargets.at(frameIndex)->GetColorImage(0).GetImageView();
    }

    void VulkanSSAOPass::Record(VkCommandBuffer commandBuffer, uint32_t frameIndex, VkDescriptorSet globalDescriptorSet, float radius, float bias, float power) const
    {
        if (globalDescriptorSet == VK_NULL_HANDLE)
        {
            throw std::runtime_error("SSAO pass requires a global descriptor set!");
        }

        const VulkanRenderTarget& renderTarget = *m_RenderTargets.at(frameIndex);
        const VkDescriptorSet gBufferDescriptorSet = m_GBufferDescriptorSets.at(frameIndex);

        VkClearValue clearValue{};
        clearValue.color = {{1.0f, 1.0f, 1.0f, 1.0f}};

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderTarget.GetRenderPass();
        renderPassInfo.framebuffer = renderTarget.GetFramebuffer();
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = renderTarget.GetExtent();
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearValue;

        const SSAOPushConstant pushConstant{radius, bias, power};

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline->GetHandle());
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline->GetLayout(), 0, 1, &globalDescriptorSet, 0, nullptr);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline->GetLayout(), 1, 1, &gBufferDescriptorSet, 0, nullptr);
        vkCmdPushConstants(commandBuffer, m_Pipeline->GetLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SSAOPushConstant), &pushConstant);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        vkCmdEndRenderPass(commandBuffer);
    }
}