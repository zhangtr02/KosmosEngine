#include "Renderer/Vulkan/VulkanSkyboxPass.h"
#include "Renderer/Vulkan/VulkanDevice.h"
#include "Renderer/Vulkan/VulkanGraphicsPipeline.h"
#include "Renderer/Vulkan/VulkanGraphicsPipelineDescription.h"

#include <filesystem>
#include <stdexcept>

namespace Kosmos
{
    VulkanSkyboxPass::VulkanSkyboxPass(
        VulkanDevice& device,
        VkRenderPass renderPass,
        VkExtent2D extent,
        VkDescriptorSetLayout globalDescriptorSetLayout)
    {
        if (globalDescriptorSetLayout == VK_NULL_HANDLE)
        {
            throw std::runtime_error(
                "Skybox pass requires a global descriptor set layout!");
        }

        VulkanGraphicsPipelineDescription description{};
        description.vertexShaderPath =
            std::filesystem::path(KOSMOS_SHADER_DIR) /
            "Skybox.vert.spv";

        description.fragmentShaderPath =
            std::filesystem::path(KOSMOS_SHADER_DIR) /
            "Skybox.frag.spv";

        description.renderPass = renderPass;
        description.extent = extent;
        description.descriptorSetLayouts = {
            globalDescriptorSetLayout
        };

        description.cullMode = VK_CULL_MODE_NONE;
        description.useDepthStencil = true;
        description.depthTestEnable = VK_FALSE;
        description.depthWriteEnable = VK_FALSE;
        description.depthCompareOp = VK_COMPARE_OP_ALWAYS;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.blendEnable = VK_FALSE;
        colorBlendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;

        description.colorBlendAttachments.push_back(
            colorBlendAttachment);

        m_Pipeline = std::make_unique<VulkanGraphicsPipeline>(
            device,
            description);
    }

    void VulkanSkyboxPass::Record(
        VkCommandBuffer commandBuffer,
        VkDescriptorSet globalDescriptorSet) const
    {
        vkCmdBindPipeline(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_Pipeline->GetHandle());

        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_Pipeline->GetLayout(),
            0,
            1,
            &globalDescriptorSet,
            0,
            nullptr);

        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    }
}