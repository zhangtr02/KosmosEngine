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
    struct PostProcessPushConstant
    {
        float exposureCompensation = 1.0f;
        float bloomIntensity = 0.08f;
    };

    static_assert(sizeof(PostProcessPushConstant) == sizeof(float) * 2);
}

namespace Kosmos
{
    VulkanFullscreenPass::VulkanFullscreenPass(VulkanDevice& device, VkRenderPass renderPass, VkExtent2D extent, const std::vector<VkImageView>& sceneColorImageViews, const std::vector<VkImageView>& bloomImageViews, const std::vector<VkImageView>& exposureImageViews)
        : m_Device(device)
    {
        if (sceneColorImageViews.empty() || sceneColorImageViews.size() != bloomImageViews.size() || sceneColorImageViews.size() != exposureImageViews.size())
        {
            throw std::runtime_error("Fullscreen pass requires matching scene color, bloom and exposure images!");
        }

        for (uint32_t frameIndex = 0; frameIndex < static_cast<uint32_t>(sceneColorImageViews.size()); ++frameIndex)
        {
            if (sceneColorImageViews[frameIndex] == VK_NULL_HANDLE || bloomImageViews[frameIndex] == VK_NULL_HANDLE || exposureImageViews[frameIndex] == VK_NULL_HANDLE)
            {
                throw std::runtime_error("Fullscreen pass contains a null input image view!");
            }
        }

        VkDescriptorSetLayoutBinding sceneColorBinding{};
        sceneColorBinding.binding = 0;
        sceneColorBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sceneColorBinding.descriptorCount = 1;
        sceneColorBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding bloomBinding{};
        bloomBinding.binding = 1;
        bloomBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bloomBinding.descriptorCount = 1;
        bloomBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding exposureBinding{};
        exposureBinding.binding = 2;
        exposureBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        exposureBinding.descriptorCount = 1;
        exposureBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        m_DescriptorSetLayout = std::make_unique<VulkanDescriptorSetLayout>(m_Device, std::vector<VkDescriptorSetLayoutBinding>{sceneColorBinding, bloomBinding, exposureBinding});

        VkDescriptorPoolSize combinedImageSamplerPoolSize{};
        combinedImageSamplerPoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        combinedImageSamplerPoolSize.descriptorCount = static_cast<uint32_t>(sceneColorImageViews.size()) * 2;

        VkDescriptorPoolSize sampledImagePoolSize{};
        sampledImagePoolSize.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        sampledImagePoolSize.descriptorCount = static_cast<uint32_t>(sceneColorImageViews.size());

        m_DescriptorPool = std::make_unique<VulkanDescriptorPool>(m_Device, static_cast<uint32_t>(sceneColorImageViews.size()), std::vector<VkDescriptorPoolSize>{combinedImageSamplerPoolSize, sampledImagePoolSize});
        m_DescriptorSets = m_DescriptorPool->AllocateSets(m_DescriptorSetLayout->GetHandle(), static_cast<uint32_t>(sceneColorImageViews.size()));

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
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;

        if (vkCreateSampler(m_Device.GetHandle(), &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create fullscreen sampler!");
        }

        VulkanDescriptorWriter writer(m_Device);

        for (uint32_t frameIndex = 0; frameIndex < static_cast<uint32_t>(sceneColorImageViews.size()); ++frameIndex)
        {
            writer.WriteImage(m_DescriptorSets[frameIndex], 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sceneColorImageViews[frameIndex], m_Sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            writer.WriteImage(m_DescriptorSets[frameIndex], 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, bloomImageViews[frameIndex], m_Sampler, VK_IMAGE_LAYOUT_GENERAL);
            writer.WriteImage(m_DescriptorSets[frameIndex], 2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, exposureImageViews[frameIndex], VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL);
        }
    }

    VulkanFullscreenPass::~VulkanFullscreenPass()
    {
        if (m_Sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(m_Device.GetHandle(), m_Sampler, nullptr);
        }
    }

    void VulkanFullscreenPass::Record(VkCommandBuffer commandBuffer, uint32_t frameIndex, float exposureCompensation, float bloomIntensity) const
    {
        const VkDescriptorSet descriptorSet = m_DescriptorSets.at(frameIndex);
        PostProcessPushConstant postProcess{};
        postProcess.exposureCompensation = exposureCompensation;
        postProcess.bloomIntensity = bloomIntensity;
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline->GetHandle());
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline->GetLayout(), 0, 1, &descriptorSet, 0, nullptr);
        vkCmdPushConstants(commandBuffer, m_Pipeline->GetLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PostProcessPushConstant), &postProcess);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    }
}
