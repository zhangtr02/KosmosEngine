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
    namespace
    {
        constexpr uint32_t ImageCount = 9;

        struct PresentPushConstant
        {
            uint32_t debugView = 0;
        };

        static_assert(sizeof(PresentPushConstant) == 4);
    }

    VulkanPresentPass::VulkanPresentPass(VulkanDevice& device, VkRenderPass renderPass, VkExtent2D extent, const std::vector<VulkanRenderViewImages>& renderViewImages)
        : m_Device(device), m_RenderPass(renderPass), m_Extent(extent)
    {
        if (m_RenderPass == VK_NULL_HANDLE || m_Extent.width == 0 || m_Extent.height == 0 || renderViewImages.empty())
        {
            throw std::runtime_error("Present pass requires valid render resources!");
        }

        std::vector<VkDescriptorSetLayoutBinding> bindings(ImageCount);

        for (uint32_t bindingIndex = 0; bindingIndex < ImageCount; ++bindingIndex)
        {
            bindings[bindingIndex].binding = bindingIndex;
            bindings[bindingIndex].descriptorType = bindingIndex == 4 ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[bindingIndex].descriptorCount = 1;
            bindings[bindingIndex].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }

        m_DescriptorSetLayout = std::make_unique<VulkanDescriptorSetLayout>(m_Device, bindings);

        const uint32_t frameCount = static_cast<uint32_t>(renderViewImages.size());
        VkDescriptorPoolSize combinedImageSamplerPoolSize{};
        combinedImageSamplerPoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        combinedImageSamplerPoolSize.descriptorCount = frameCount * (ImageCount - 1);

        VkDescriptorPoolSize sampledImagePoolSize{};
        sampledImagePoolSize.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        sampledImagePoolSize.descriptorCount = frameCount;

        m_DescriptorPool = std::make_unique<VulkanDescriptorPool>(m_Device, frameCount, std::vector<VkDescriptorPoolSize>{combinedImageSamplerPoolSize, sampledImagePoolSize});
        m_DescriptorSets = m_DescriptorPool->AllocateSets(m_DescriptorSetLayout->GetHandle(), static_cast<uint32_t>(renderViewImages.size()));

        VulkanGraphicsPipelineDescription pipelineDescription{};
        pipelineDescription.vertexShaderPath = std::filesystem::path(KOSMOS_SHADER_DIR) / "Fullscreen.vert.spv";
        pipelineDescription.fragmentShaderPath = std::filesystem::path(KOSMOS_SHADER_DIR) / "Present.frag.spv";
        pipelineDescription.renderPass = m_RenderPass;
        pipelineDescription.extent = m_Extent;
        pipelineDescription.descriptorSetLayouts.push_back(m_DescriptorSetLayout->GetHandle());
        pipelineDescription.cullMode = VK_CULL_MODE_NONE;
        pipelineDescription.useDepthStencil = false;

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(PresentPushConstant);
        pipelineDescription.pushConstantRanges.push_back(pushConstantRange);

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

        for (uint32_t frameIndex = 0; frameIndex < static_cast<uint32_t>(renderViewImages.size()); ++frameIndex)
        {
            const VulkanRenderViewImages& images = renderViewImages[frameIndex];

            if (images.finalColor == VK_NULL_HANDLE || images.albedoAmbientOcclusion == VK_NULL_HANDLE || images.normalRoughness == VK_NULL_HANDLE || images.materialParameters == VK_NULL_HANDLE || images.depth == VK_NULL_HANDLE || images.rawAmbientOcclusion == VK_NULL_HANDLE || images.ambientOcclusion == VK_NULL_HANDLE || images.sceneColor == VK_NULL_HANDLE || images.bloom == VK_NULL_HANDLE)
            {
                throw std::runtime_error("Present pass contains a null debug-view image!");
            }

            const VkDescriptorSet descriptorSet = m_DescriptorSets[frameIndex];
            writer.WriteImage(descriptorSet, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, images.finalColor, m_Sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            writer.WriteImage(descriptorSet, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, images.albedoAmbientOcclusion, m_Sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            writer.WriteImage(descriptorSet, 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, images.normalRoughness, m_Sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            writer.WriteImage(descriptorSet, 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, images.materialParameters, m_Sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            writer.WriteImage(descriptorSet, 4, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, images.depth, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
            writer.WriteImage(descriptorSet, 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, images.rawAmbientOcclusion, m_Sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            writer.WriteImage(descriptorSet, 6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, images.ambientOcclusion, m_Sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            writer.WriteImage(descriptorSet, 7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, images.sceneColor, m_Sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            writer.WriteImage(descriptorSet, 8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, images.bloom, m_Sampler, VK_IMAGE_LAYOUT_GENERAL);
        }
    }

    VulkanPresentPass::~VulkanPresentPass()
    {
        if (m_Sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(m_Device.GetHandle(), m_Sampler, nullptr);
        }
    }

    void VulkanPresentPass::Record(VkCommandBuffer commandBuffer, VkFramebuffer framebuffer, uint32_t frameIndex, RenderDebugView debugView) const
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
        const PresentPushConstant pushConstant{static_cast<uint32_t>(debugView)};
        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline->GetHandle());
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline->GetLayout(), 0, 1, &descriptorSet, 0, nullptr);
        vkCmdPushConstants(commandBuffer, m_Pipeline->GetLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PresentPushConstant), &pushConstant);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        vkCmdEndRenderPass(commandBuffer);
    }
}
