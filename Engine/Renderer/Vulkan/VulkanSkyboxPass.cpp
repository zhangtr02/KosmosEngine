#include "Renderer/Vulkan/VulkanSkyboxPass.h"
#include "Renderer/Vulkan/VulkanCubeTexture.h"
#include "Renderer/Vulkan/VulkanDevice.h"
#include "Renderer/Vulkan/VulkanDescriptorSetLayout.h"
#include "Renderer/Vulkan/VulkanDescriptorPool.h"
#include "Renderer/Vulkan/VulkanDescriptorWriter.h"
#include "Renderer/Vulkan/VulkanGraphicsPipeline.h"
#include "Renderer/Vulkan/VulkanGraphicsPipelineDescription.h"

#include <array>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace Kosmos
{
    VulkanSkyboxPass::VulkanSkyboxPass(VulkanDevice& device, VkRenderPass renderPass, VkExtent2D extent, VkDescriptorSetLayout globalDescriptorSetLayout, const VulkanCubeTexture& environment)
    {
        if (globalDescriptorSetLayout == VK_NULL_HANDLE)
        {
            throw std::runtime_error("Skybox pass requires a global descriptor set layout!");
        }

        VkDescriptorSetLayoutBinding environmentBinding{};
        environmentBinding.binding = 0;
        environmentBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        environmentBinding.descriptorCount = 1;
        environmentBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        m_DescriptorSetLayout = std::make_unique<VulkanDescriptorSetLayout>(device, std::vector<VkDescriptorSetLayoutBinding>{environmentBinding});

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 1;

        m_DescriptorPool = std::make_unique<VulkanDescriptorPool>(device, 1, std::vector<VkDescriptorPoolSize>{poolSize});
        m_DescriptorSet = m_DescriptorPool->AllocateSets(m_DescriptorSetLayout->GetHandle(), 1).front();

        VulkanDescriptorWriter writer(device);
        writer.WriteImage(m_DescriptorSet, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, environment.GetImageView(), environment.GetSampler(), environment.GetLayout());

        VulkanGraphicsPipelineDescription description{};
        description.vertexShaderPath = std::filesystem::path(KOSMOS_SHADER_DIR) / "Skybox.vert.spv";
        description.fragmentShaderPath = std::filesystem::path(KOSMOS_SHADER_DIR) / "Skybox.frag.spv";
        description.renderPass = renderPass;
        description.extent = extent;
        description.descriptorSetLayouts = {
            globalDescriptorSetLayout,
            m_DescriptorSetLayout->GetHandle()
        };
        description.cullMode = VK_CULL_MODE_NONE;
        description.useDepthStencil = true;
        description.depthTestEnable = VK_FALSE;
        description.depthWriteEnable = VK_FALSE;
        description.depthCompareOp = VK_COMPARE_OP_ALWAYS;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.blendEnable = VK_FALSE;
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        description.colorBlendAttachments.push_back(colorBlendAttachment);

        m_Pipeline = std::make_unique<VulkanGraphicsPipeline>(device, description);
    }

    void VulkanSkyboxPass::Record(VkCommandBuffer commandBuffer, VkDescriptorSet globalDescriptorSet) const
    {
        const std::array<VkDescriptorSet, 2> descriptorSets = {
            globalDescriptorSet,
            m_DescriptorSet
        };

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline->GetHandle());
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline->GetLayout(), 0, static_cast<uint32_t>(descriptorSets.size()), descriptorSets.data(), 0, nullptr);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    }
}