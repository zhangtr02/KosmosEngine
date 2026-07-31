#include "Renderer/Vulkan/View/VulkanRenderView.h"
#include "Renderer/Resources/Vertex.h"
#include "Renderer/ShaderData/ObjectPushConstant.h"
#include "Renderer/Vulkan/Core/VulkanDevice.h"
#include "Renderer/Vulkan/RenderTargets/VulkanGBuffer.h"
#include "Renderer/Vulkan/Pipelines/VulkanGraphicsPipeline.h"
#include "Renderer/Vulkan/Pipelines/VulkanGraphicsPipelineDescription.h"
#include "Renderer/Vulkan/Passes/VulkanSSAOPass.h"
#include "Renderer/Vulkan/RenderTargets/VulkanRenderTarget.h"
#include "Renderer/Vulkan/RenderTargets/VulkanRenderTargetDescription.h"
#include "Renderer/Vulkan/Resources/VulkanImage.h"
#include "Renderer/Vulkan/Passes/VulkanSkyboxPass.h"
#include "Renderer/Vulkan/Passes/VulkanDeferredLightingPass.h"
#include "Renderer/Vulkan/Passes/VulkanAutoExposurePass.h"
#include "Renderer/Vulkan/Passes/VulkanBloomPass.h"
#include "Renderer/Vulkan/Passes/VulkanFullscreenPass.h"

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace
{
    constexpr VkFormat SceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    constexpr VkFormat OutputColorFormat = VK_FORMAT_R8G8B8A8_SRGB;
}

namespace Kosmos
{
    VulkanRenderView::VulkanRenderView(VulkanDevice& device, VkExtent2D extent, uint32_t frameCount, VkDescriptorSetLayout globalDescriptorSetLayout, VkDescriptorSetLayout materialDescriptorSetLayout)
        : m_Device(device), m_Extent(extent)
    {
        if (m_Extent.width == 0 || m_Extent.height == 0 || frameCount == 0 || globalDescriptorSetLayout == VK_NULL_HANDLE || materialDescriptorSetLayout == VK_NULL_HANDLE)
        {
            throw std::runtime_error("Vulkan render view requires valid resources!");
        }

        m_GBuffers.reserve(frameCount);
        m_SceneRenderTargets.reserve(frameCount);
        m_OutputRenderTargets.reserve(frameCount);

        std::vector<VkImageView> sceneColorImageViews;
        std::vector<const VulkanGBuffer*> gBuffers;
        sceneColorImageViews.reserve(frameCount);
        gBuffers.reserve(frameCount);

        VulkanRenderTargetDescription sceneRenderTargetDescription{};
        sceneRenderTargetDescription.extent = m_Extent;

        VulkanRenderTargetColorAttachmentDescription sceneColorAttachment{};
        sceneColorAttachment.format = SceneColorFormat;
        sceneColorAttachment.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        sceneColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        sceneColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        sceneColorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        sceneColorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sceneRenderTargetDescription.colorAttachments.push_back(sceneColorAttachment);

        VulkanRenderTargetDescription outputRenderTargetDescription{};
        outputRenderTargetDescription.extent = m_Extent;

        VulkanRenderTargetColorAttachmentDescription outputColorAttachment{};
        outputColorAttachment.format = OutputColorFormat;
        outputColorAttachment.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        outputColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        outputColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        outputColorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        outputColorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        outputRenderTargetDescription.colorAttachments.push_back(outputColorAttachment);

        for (uint32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
        {
            m_GBuffers.push_back(std::make_unique<VulkanGBuffer>(m_Device, m_Extent));
            m_SceneRenderTargets.push_back(std::make_unique<VulkanRenderTarget>(m_Device, sceneRenderTargetDescription));
            m_OutputRenderTargets.push_back(std::make_unique<VulkanRenderTarget>(m_Device, outputRenderTargetDescription));
            sceneColorImageViews.push_back(m_SceneRenderTargets.back()->GetColorImage(0).GetImageView());
            gBuffers.push_back(m_GBuffers.back().get());
        }

        m_GBufferPipeline = CreateGBufferPipeline(globalDescriptorSetLayout, materialDescriptorSetLayout);
        m_SSAOPass = std::make_unique<VulkanSSAOPass>(m_Device, m_Extent, globalDescriptorSetLayout, gBuffers);

        std::vector<VkImageView> ambientOcclusionImageViews;
        ambientOcclusionImageViews.reserve(frameCount);

        for (uint32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
        {
            ambientOcclusionImageViews.push_back(m_SSAOPass->GetAmbientOcclusionImageView(frameIndex));
        }

        m_SkyboxPass = std::make_unique<VulkanSkyboxPass>(m_Device, m_SceneRenderTargets.front()->GetRenderPass(), m_Extent, globalDescriptorSetLayout);
        m_DeferredLightingPass = std::make_unique<VulkanDeferredLightingPass>(m_Device, m_SceneRenderTargets.front()->GetRenderPass(), m_Extent, globalDescriptorSetLayout, gBuffers, ambientOcclusionImageViews);
        m_AutoExposurePass = std::make_unique<VulkanAutoExposurePass>(m_Device, m_Extent, sceneColorImageViews);
        m_BloomPass = std::make_unique<VulkanBloomPass>(m_Device, m_Extent, sceneColorImageViews);

        std::vector<VkImageView> bloomImageViews;
        std::vector<VkImageView> exposureImageViews;
        bloomImageViews.reserve(frameCount);
        exposureImageViews.reserve(frameCount);

        for (uint32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
        {
            bloomImageViews.push_back(m_BloomPass->GetBloomImageView(frameIndex));
            exposureImageViews.push_back(m_AutoExposurePass->GetExposureImageView(frameIndex));
        }

        m_FullscreenPass = std::make_unique<VulkanFullscreenPass>(m_Device, m_OutputRenderTargets.front()->GetRenderPass(), m_Extent, sceneColorImageViews, bloomImageViews, exposureImageViews);
    }

    VulkanRenderView::~VulkanRenderView() = default;

    std::unique_ptr<VulkanGraphicsPipeline> VulkanRenderView::CreateGBufferPipeline(VkDescriptorSetLayout globalDescriptorSetLayout, VkDescriptorSetLayout materialDescriptorSetLayout)
    {
        VulkanGraphicsPipelineDescription description{};
        description.vertexShaderPath = std::filesystem::path(KOSMOS_SHADER_DIR) / "GBuffer.vert.spv";
        description.fragmentShaderPath = std::filesystem::path(KOSMOS_SHADER_DIR) / "GBuffer.frag.spv";
        description.renderPass = m_GBuffers.front()->GetRenderPass();
        description.extent = m_Extent;
        description.descriptorSetLayouts = {globalDescriptorSetLayout, materialDescriptorSetLayout};

        VkPushConstantRange objectPushConstantRange{};
        objectPushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        objectPushConstantRange.offset = 0;
        objectPushConstantRange.size = sizeof(ObjectPushConstant);
        description.pushConstantRanges.push_back(objectPushConstantRange);

        description.vertexBindings.push_back({0, static_cast<uint32_t>(sizeof(Vertex)), VK_VERTEX_INPUT_RATE_VERTEX});
        description.vertexAttributes = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, position))},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, color))},
            {2, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, textureCoordinate))},
            {3, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, normal))},
            {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, tangent))}
        };

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.blendEnable = VK_FALSE;
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        description.colorBlendAttachments.resize(VulkanGBuffer::ColorAttachmentCount, colorBlendAttachment);
        description.cullMode = VK_CULL_MODE_BACK_BIT;
        description.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        description.useDepthStencil = true;
        description.depthTestEnable = VK_TRUE;
        description.depthWriteEnable = VK_TRUE;
        description.depthCompareOp = VK_COMPARE_OP_LESS;
        return std::make_unique<VulkanGraphicsPipeline>(m_Device, description);
    }

    VulkanGBuffer& VulkanRenderView::GetGBuffer(uint32_t frameIndex) const
    {
        return *m_GBuffers.at(frameIndex);
    }

    VulkanGraphicsPipeline& VulkanRenderView::GetGBufferPipeline() const
    {
        return *m_GBufferPipeline;
    }

    VulkanSSAOPass& VulkanRenderView::GetSSAOPass() const
    {
        return *m_SSAOPass;
    }

    VulkanRenderTarget& VulkanRenderView::GetSceneRenderTarget(uint32_t frameIndex) const
    {
        return *m_SceneRenderTargets.at(frameIndex);
    }

    VulkanSkyboxPass& VulkanRenderView::GetSkyboxPass() const
    {
        return *m_SkyboxPass;
    }

    VulkanDeferredLightingPass& VulkanRenderView::GetDeferredLightingPass() const
    {
        return *m_DeferredLightingPass;
    }

    VulkanAutoExposurePass& VulkanRenderView::GetAutoExposurePass() const
    {
        return *m_AutoExposurePass;
    }

    VulkanBloomPass& VulkanRenderView::GetBloomPass() const
    {
        return *m_BloomPass;
    }

    VulkanFullscreenPass& VulkanRenderView::GetFullscreenPass() const
    {
        return *m_FullscreenPass;
    }

    VulkanRenderTarget& VulkanRenderView::GetOutputRenderTarget(uint32_t frameIndex) const
    {
        return *m_OutputRenderTargets.at(frameIndex);
    }

    VulkanRenderViewImages VulkanRenderView::GetImages(uint32_t frameIndex) const
    {
        const VulkanGBuffer& gBuffer = *m_GBuffers.at(frameIndex);

        VulkanRenderViewImages images{};
        images.finalColor = m_OutputRenderTargets.at(frameIndex)->GetColorImage(0).GetImageView();
        images.albedoAmbientOcclusion = gBuffer.GetAlbedoAmbientOcclusionImageView();
        images.normalRoughness = gBuffer.GetNormalRoughnessImageView();
        images.materialParameters = gBuffer.GetMaterialParametersImageView();
        images.depth = gBuffer.GetDepthImageView();
        images.rawAmbientOcclusion = m_SSAOPass->GetRawAmbientOcclusionImageView(frameIndex);
        images.ambientOcclusion = m_SSAOPass->GetAmbientOcclusionImageView(frameIndex);
        images.sceneColor = m_SceneRenderTargets.at(frameIndex)->GetColorImage(0).GetImageView();
        images.bloom = m_BloomPass->GetBloomImageView(frameIndex);
        return images;
    }
}
