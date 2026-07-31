#include "Renderer/Vulkan/VulkanDeferredLightingPass.h"
#include "Renderer/Vulkan/VulkanDevice.h"
#include "Renderer/Vulkan/VulkanGBuffer.h"
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
    VulkanDeferredLightingPass::VulkanDeferredLightingPass(VulkanDevice& device, VkRenderPass renderPass, VkExtent2D extent, VkDescriptorSetLayout globalDescriptorSetLayout, const std::vector<const VulkanGBuffer*>& gBuffers)
    {
        if (renderPass == VK_NULL_HANDLE || globalDescriptorSetLayout == VK_NULL_HANDLE || extent.width == 0 || extent.height == 0 || gBuffers.empty())
        {
            throw std::runtime_error("Deferred lighting pass requires valid render resources!");
        }

        std::vector<VkDescriptorSetLayoutBinding> bindings(4);

        for (uint32_t bindingIndex = 0; bindingIndex < static_cast<uint32_t>(bindings.size()); ++bindingIndex)
        {
            bindings[bindingIndex].binding = bindingIndex;
            bindings[bindingIndex].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            bindings[bindingIndex].descriptorCount = 1;
            bindings[bindingIndex].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }

        m_GBufferDescriptorSetLayout = std::make_unique<VulkanDescriptorSetLayout>(device, bindings);

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        poolSize.descriptorCount = static_cast<uint32_t>(gBuffers.size()) * 4;

        m_DescriptorPool = std::make_unique<VulkanDescriptorPool>(device, static_cast<uint32_t>(gBuffers.size()), std::vector<VkDescriptorPoolSize>{poolSize});
        m_GBufferDescriptorSets = m_DescriptorPool->AllocateSets(m_GBufferDescriptorSetLayout->GetHandle(), static_cast<uint32_t>(gBuffers.size()));

        VulkanDescriptorWriter writer(device);

        for (uint32_t frameIndex = 0; frameIndex < static_cast<uint32_t>(gBuffers.size()); ++frameIndex)
        {
            const VulkanGBuffer* gBuffer = gBuffers[frameIndex];

            if (!gBuffer)
            {
                throw std::runtime_error("Deferred lighting pass contains a null G-buffer!");
            }

            writer.WriteImage(m_GBufferDescriptorSets[frameIndex], 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, gBuffer->GetAlbedoAmbientOcclusionImageView(), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            writer.WriteImage(m_GBufferDescriptorSets[frameIndex], 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, gBuffer->GetNormalRoughnessImageView(), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            writer.WriteImage(m_GBufferDescriptorSets[frameIndex], 2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, gBuffer->GetMaterialParametersImageView(), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            writer.WriteImage(m_GBufferDescriptorSets[frameIndex], 3, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, gBuffer->GetDepthImageView(), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        }

        VulkanGraphicsPipelineDescription description{};
        description.vertexShaderPath = std::filesystem::path(KOSMOS_SHADER_DIR) / "Fullscreen.vert.spv";
        description.fragmentShaderPath = std::filesystem::path(KOSMOS_SHADER_DIR) / "DeferredLighting.frag.spv";
        description.renderPass = renderPass;
        description.extent = extent;
        description.descriptorSetLayouts = {globalDescriptorSetLayout, m_GBufferDescriptorSetLayout->GetHandle()};
        description.cullMode = VK_CULL_MODE_NONE;
        description.useDepthStencil = false;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.blendEnable = VK_FALSE;
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        description.colorBlendAttachments.push_back(colorBlendAttachment);

        m_Pipeline = std::make_unique<VulkanGraphicsPipeline>(device, description);
    }

    VulkanDeferredLightingPass::~VulkanDeferredLightingPass() = default;

    void VulkanDeferredLightingPass::Record(VkCommandBuffer commandBuffer, uint32_t frameIndex, VkDescriptorSet globalDescriptorSet) const
    {
        if (globalDescriptorSet == VK_NULL_HANDLE)
        {
            throw std::runtime_error("Deferred lighting pass requires a global descriptor set!");
        }

        const VkDescriptorSet gBufferDescriptorSet = m_GBufferDescriptorSets.at(frameIndex);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline->GetHandle());
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline->GetLayout(), 0, 1, &globalDescriptorSet, 0, nullptr);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline->GetLayout(), 1, 1, &gBufferDescriptorSet, 0, nullptr);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    }
}