#include "Renderer/Vulkan/VulkanDirectionalShadowPass.h"
#include "Renderer/Vulkan/VulkanDevice.h"
#include "Renderer/Vulkan/VulkanRenderTarget.h"
#include "Renderer/Vulkan/VulkanRenderTargetDescription.h"
#include "Renderer/Vulkan/VulkanGraphicsPipeline.h"
#include "Renderer/Vulkan/VulkanGraphicsPipelineDescription.h"
#include "Renderer/Vulkan/VulkanMesh.h"
#include "Renderer/Vulkan/VulkanImage.h"
#include "Renderer/Vertex.h"
#include "Scene/Scene.h"
#include "Scene/Light.h"

#include <glm/glm.hpp>
#include <cstddef>
#include <filesystem>
#include <stdexcept>

namespace
{
    struct ShadowPushConstant
    {
        glm::mat4 model{1.0f};
    };

    static_assert(sizeof(ShadowPushConstant) == 64);
}

namespace Kosmos
{
    VulkanDirectionalShadowPass::VulkanDirectionalShadowPass(VulkanDevice& device, const Scene& scene, const std::unordered_map<const Mesh*, std::unique_ptr<VulkanMesh>>& meshes, VkDescriptorSetLayout globalDescriptorSetLayout, uint32_t frameCount)
        : m_Device(device), m_Scene(scene), m_Meshes(meshes)
    {
        if (globalDescriptorSetLayout == VK_NULL_HANDLE)
        {
            throw std::runtime_error("Directional shadow pass requires a global descriptor set layout!");
        }

        if (frameCount == 0)
        {
            throw std::runtime_error("Directional shadow pass requires at least one frame!");
        }

        const uint32_t resolution = m_Scene.GetLighting().directionalLight.shadowMapResolution;

        VulkanRenderTargetDescription renderTargetDescription{};
        renderTargetDescription.extent = {resolution, resolution};

        VulkanRenderTargetDepthAttachmentDescription depthAttachment{};
        depthAttachment.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        renderTargetDescription.depthAttachment = depthAttachment;

        m_RenderTargets.reserve(frameCount);

        for (uint32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
        {
            m_RenderTargets.push_back(std::make_unique<VulkanRenderTarget>(m_Device, renderTargetDescription));
        }

        VulkanGraphicsPipelineDescription pipelineDescription{};
        pipelineDescription.vertexShaderPath = std::filesystem::path(KOSMOS_SHADER_DIR) / "ShadowDepth.vert.spv";
        pipelineDescription.renderPass = m_RenderTargets.front()->GetRenderPass();
        pipelineDescription.extent = m_RenderTargets.front()->GetExtent();
        pipelineDescription.descriptorSetLayouts.push_back(globalDescriptorSetLayout);

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(ShadowPushConstant);
        pipelineDescription.pushConstantRanges.push_back(pushConstantRange);

        pipelineDescription.vertexBindings.push_back({
            0,
            static_cast<uint32_t>(sizeof(Vertex)),
            VK_VERTEX_INPUT_RATE_VERTEX
        });

        pipelineDescription.vertexAttributes.push_back({
            0,
            0,
            VK_FORMAT_R32G32B32_SFLOAT,
            static_cast<uint32_t>(offsetof(Vertex, position))
        });

        pipelineDescription.cullMode = VK_CULL_MODE_BACK_BIT;
        pipelineDescription.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        pipelineDescription.depthBiasEnable = VK_TRUE;
        pipelineDescription.depthBiasConstantFactor = 1.25f;
        pipelineDescription.depthBiasSlopeFactor = 1.75f;
        pipelineDescription.useDepthStencil = true;
        pipelineDescription.depthTestEnable = VK_TRUE;
        pipelineDescription.depthWriteEnable = VK_TRUE;
        pipelineDescription.depthCompareOp = VK_COMPARE_OP_LESS;

        m_Pipeline = std::make_unique<VulkanGraphicsPipeline>(m_Device, pipelineDescription);

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.compareEnable = VK_TRUE;
        samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        if (vkCreateSampler(m_Device.GetHandle(), &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create directional shadow sampler!");
        }
    }

    VulkanDirectionalShadowPass::~VulkanDirectionalShadowPass()
    {
        if (m_Sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(m_Device.GetHandle(), m_Sampler, nullptr);
        }
    }

    VkImageView VulkanDirectionalShadowPass::GetShadowMapImageView(uint32_t frameIndex) const
    {
        const VulkanImage* depthImage = m_RenderTargets.at(frameIndex)->GetDepthImage();

        if (!depthImage)
        {
            throw std::runtime_error("Directional shadow render target does not contain a depth image!");
        }

        return depthImage->GetImageView();
    }

    void VulkanDirectionalShadowPass::Record(VkCommandBuffer commandBuffer, uint32_t frameIndex, VkDescriptorSet globalDescriptorSet) const
    {
        if (globalDescriptorSet == VK_NULL_HANDLE)
        {
            throw std::runtime_error("Cannot record directional shadow pass with a null descriptor set!");
        }

        const VulkanRenderTarget& renderTarget = *m_RenderTargets.at(frameIndex);

        VkClearValue clearValue{};
        clearValue.depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderTarget.GetRenderPass();
        renderPassInfo.framebuffer = renderTarget.GetFramebuffer();
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = renderTarget.GetExtent();
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearValue;

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline->GetHandle());
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline->GetLayout(), 0, 1, &globalDescriptorSet, 0, nullptr);

        const VulkanMesh* boundMesh = nullptr;

        for (const RenderObject& object : m_Scene.GetRenderObjects())
        {
            const VulkanMesh& mesh = *m_Meshes.at(object.mesh.get());

            if (boundMesh != &mesh)
            {
                mesh.Bind(commandBuffer);
                boundMesh = &mesh;
            }

            ShadowPushConstant pushConstant{};
            pushConstant.model = object.transform.GetMatrix();

            vkCmdPushConstants(commandBuffer, m_Pipeline->GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPushConstant), &pushConstant);
            mesh.Draw(commandBuffer);
        }

        vkCmdEndRenderPass(commandBuffer);
    }
}