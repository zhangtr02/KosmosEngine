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

    struct SSAOBlurPushConstant
    {
        int32_t directionX = 0;
        int32_t directionY = 0;
        float depthSharpness = 4.0f;
        float normalSharpness = 16.0f;
    };

    static_assert(sizeof(SSAOPushConstant) == 12);
    static_assert(sizeof(SSAOBlurPushConstant) == 16);
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

        m_RawRenderTargets.reserve(gBuffers.size());
        m_BlurIntermediateRenderTargets.reserve(gBuffers.size());
        m_BlurredRenderTargets.reserve(gBuffers.size());

        for (const VulkanGBuffer* gBuffer : gBuffers)
        {
            if (!gBuffer)
            {
                throw std::runtime_error("SSAO pass contains a null G-buffer!");
            }

            m_RawRenderTargets.push_back(std::make_unique<VulkanRenderTarget>(m_Device, renderTargetDescription));
            m_BlurIntermediateRenderTargets.push_back(std::make_unique<VulkanRenderTarget>(m_Device, renderTargetDescription));
            m_BlurredRenderTargets.push_back(std::make_unique<VulkanRenderTarget>(m_Device, renderTargetDescription));
        }

        std::vector<VkDescriptorSetLayoutBinding> gBufferBindings(2);

        for (uint32_t bindingIndex = 0; bindingIndex < static_cast<uint32_t>(gBufferBindings.size()); ++bindingIndex)
        {
            gBufferBindings[bindingIndex].binding = bindingIndex;
            gBufferBindings[bindingIndex].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            gBufferBindings[bindingIndex].descriptorCount = 1;
            gBufferBindings[bindingIndex].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }

        m_GBufferDescriptorSetLayout = std::make_unique<VulkanDescriptorSetLayout>(m_Device, gBufferBindings);

        std::vector<VkDescriptorSetLayoutBinding> blurBindings(3);

        for (uint32_t bindingIndex = 0; bindingIndex < static_cast<uint32_t>(blurBindings.size()); ++bindingIndex)
        {
            blurBindings[bindingIndex].binding = bindingIndex;
            blurBindings[bindingIndex].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            blurBindings[bindingIndex].descriptorCount = 1;
            blurBindings[bindingIndex].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }

        m_BlurDescriptorSetLayout = std::make_unique<VulkanDescriptorSetLayout>(m_Device, blurBindings);

        const uint32_t frameCount = static_cast<uint32_t>(gBuffers.size());

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        poolSize.descriptorCount = frameCount * 8;

        m_DescriptorPool = std::make_unique<VulkanDescriptorPool>(m_Device, frameCount * 3, std::vector<VkDescriptorPoolSize>{poolSize});
        m_GBufferDescriptorSets = m_DescriptorPool->AllocateSets(m_GBufferDescriptorSetLayout->GetHandle(), frameCount);
        m_HorizontalBlurDescriptorSets = m_DescriptorPool->AllocateSets(m_BlurDescriptorSetLayout->GetHandle(), frameCount);
        m_VerticalBlurDescriptorSets = m_DescriptorPool->AllocateSets(m_BlurDescriptorSetLayout->GetHandle(), frameCount);

        VulkanDescriptorWriter writer(m_Device);

        for (uint32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
        {
            const VulkanGBuffer& gBuffer = *gBuffers[frameIndex];

            writer.WriteImage(m_GBufferDescriptorSets[frameIndex], 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, gBuffer.GetNormalRoughnessImageView(), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            writer.WriteImage(m_GBufferDescriptorSets[frameIndex], 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, gBuffer.GetDepthImageView(), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

            writer.WriteImage(m_HorizontalBlurDescriptorSets[frameIndex], 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, m_RawRenderTargets[frameIndex]->GetColorImage(0).GetImageView(), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            writer.WriteImage(m_HorizontalBlurDescriptorSets[frameIndex], 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, gBuffer.GetNormalRoughnessImageView(), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            writer.WriteImage(m_HorizontalBlurDescriptorSets[frameIndex], 2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, gBuffer.GetDepthImageView(), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

            writer.WriteImage(m_VerticalBlurDescriptorSets[frameIndex], 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, m_BlurIntermediateRenderTargets[frameIndex]->GetColorImage(0).GetImageView(), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            writer.WriteImage(m_VerticalBlurDescriptorSets[frameIndex], 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, gBuffer.GetNormalRoughnessImageView(), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            writer.WriteImage(m_VerticalBlurDescriptorSets[frameIndex], 2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, gBuffer.GetDepthImageView(), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        }

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.blendEnable = VK_FALSE;
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;

        VulkanGraphicsPipelineDescription rawPipelineDescription{};
        rawPipelineDescription.vertexShaderPath = std::filesystem::path(KOSMOS_SHADER_DIR) / "Fullscreen.vert.spv";
        rawPipelineDescription.fragmentShaderPath = std::filesystem::path(KOSMOS_SHADER_DIR) / "SSAO.frag.spv";
        rawPipelineDescription.renderPass = m_RawRenderTargets.front()->GetRenderPass();
        rawPipelineDescription.extent = extent;
        rawPipelineDescription.descriptorSetLayouts = {globalDescriptorSetLayout, m_GBufferDescriptorSetLayout->GetHandle()};
        rawPipelineDescription.cullMode = VK_CULL_MODE_NONE;
        rawPipelineDescription.useDepthStencil = false;
        rawPipelineDescription.colorBlendAttachments.push_back(colorBlendAttachment);

        VkPushConstantRange rawPushConstantRange{};
        rawPushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        rawPushConstantRange.offset = 0;
        rawPushConstantRange.size = sizeof(SSAOPushConstant);
        rawPipelineDescription.pushConstantRanges.push_back(rawPushConstantRange);

        m_RawPipeline = std::make_unique<VulkanGraphicsPipeline>(m_Device, rawPipelineDescription);

        VulkanGraphicsPipelineDescription blurPipelineDescription{};
        blurPipelineDescription.vertexShaderPath = std::filesystem::path(KOSMOS_SHADER_DIR) / "Fullscreen.vert.spv";
        blurPipelineDescription.fragmentShaderPath = std::filesystem::path(KOSMOS_SHADER_DIR) / "SSAOBlur.frag.spv";
        blurPipelineDescription.renderPass = m_BlurIntermediateRenderTargets.front()->GetRenderPass();
        blurPipelineDescription.extent = extent;
        blurPipelineDescription.descriptorSetLayouts = {globalDescriptorSetLayout, m_BlurDescriptorSetLayout->GetHandle()};
        blurPipelineDescription.cullMode = VK_CULL_MODE_NONE;
        blurPipelineDescription.useDepthStencil = false;
        blurPipelineDescription.colorBlendAttachments.push_back(colorBlendAttachment);

        VkPushConstantRange blurPushConstantRange{};
        blurPushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        blurPushConstantRange.offset = 0;
        blurPushConstantRange.size = sizeof(SSAOBlurPushConstant);
        blurPipelineDescription.pushConstantRanges.push_back(blurPushConstantRange);

        m_BlurPipeline = std::make_unique<VulkanGraphicsPipeline>(m_Device, blurPipelineDescription);
    }

    VulkanSSAOPass::~VulkanSSAOPass() = default;

    VkImageView VulkanSSAOPass::GetAmbientOcclusionImageView(uint32_t frameIndex) const
    {
        return m_BlurredRenderTargets.at(frameIndex)->GetColorImage(0).GetImageView();
    }

    void VulkanSSAOPass::RecordBlurPass(VkCommandBuffer commandBuffer, const VulkanRenderTarget& renderTarget, VkDescriptorSet descriptorSet, VkDescriptorSet globalDescriptorSet, int32_t directionX, int32_t directionY, float depthSharpness, float normalSharpness) const
    {
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

        const SSAOBlurPushConstant pushConstant{directionX, directionY, depthSharpness, normalSharpness};

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_BlurPipeline->GetHandle());
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_BlurPipeline->GetLayout(), 0, 1, &globalDescriptorSet, 0, nullptr);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_BlurPipeline->GetLayout(), 1, 1, &descriptorSet, 0, nullptr);
        vkCmdPushConstants(commandBuffer, m_BlurPipeline->GetLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SSAOBlurPushConstant), &pushConstant);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        vkCmdEndRenderPass(commandBuffer);
    }

    void VulkanSSAOPass::Record(VkCommandBuffer commandBuffer, uint32_t frameIndex, VkDescriptorSet globalDescriptorSet, float radius, float bias, float power, float depthSharpness, float normalSharpness) const
    {
        if (globalDescriptorSet == VK_NULL_HANDLE)
        {
            throw std::runtime_error("SSAO pass requires a global descriptor set!");
        }

        const VulkanRenderTarget& rawRenderTarget = *m_RawRenderTargets.at(frameIndex);
        const VkDescriptorSet gBufferDescriptorSet = m_GBufferDescriptorSets.at(frameIndex);

        VkClearValue clearValue{};
        clearValue.color = {{1.0f, 1.0f, 1.0f, 1.0f}};

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = rawRenderTarget.GetRenderPass();
        renderPassInfo.framebuffer = rawRenderTarget.GetFramebuffer();
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = rawRenderTarget.GetExtent();
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearValue;

        const SSAOPushConstant pushConstant{radius, bias, power};

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_RawPipeline->GetHandle());
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_RawPipeline->GetLayout(), 0, 1, &globalDescriptorSet, 0, nullptr);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_RawPipeline->GetLayout(), 1, 1, &gBufferDescriptorSet, 0, nullptr);
        vkCmdPushConstants(commandBuffer, m_RawPipeline->GetLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SSAOPushConstant), &pushConstant);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        vkCmdEndRenderPass(commandBuffer);

        RecordBlurPass(commandBuffer, *m_BlurIntermediateRenderTargets.at(frameIndex), m_HorizontalBlurDescriptorSets.at(frameIndex), globalDescriptorSet, 1, 0, depthSharpness, normalSharpness);
        RecordBlurPass(commandBuffer, *m_BlurredRenderTargets.at(frameIndex), m_VerticalBlurDescriptorSets.at(frameIndex), globalDescriptorSet, 0, 1, depthSharpness, normalSharpness);
    }
}