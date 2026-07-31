#pragma once

#include "Renderer/Vulkan/View/VulkanRenderViewImages.h"

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

namespace Kosmos
{
    class VulkanDevice;
    class VulkanGBuffer;
    class VulkanGraphicsPipeline;
    class VulkanSSAOPass;
    class VulkanRenderTarget;
    class VulkanSkyboxPass;
    class VulkanDeferredLightingPass;
    class VulkanAutoExposurePass;
    class VulkanBloomPass;
    class VulkanFullscreenPass;

    class VulkanRenderView
    {
        public:
            VulkanRenderView(VulkanDevice& device, VkExtent2D extent, uint32_t frameCount, VkDescriptorSetLayout globalDescriptorSetLayout, VkDescriptorSetLayout materialDescriptorSetLayout);
            ~VulkanRenderView();

            VulkanRenderView(const VulkanRenderView&) = delete;
            VulkanRenderView& operator=(const VulkanRenderView&) = delete;

            VkExtent2D GetExtent() const { return m_Extent; }
            VulkanGBuffer& GetGBuffer(uint32_t frameIndex) const;
            VulkanGraphicsPipeline& GetGBufferPipeline() const;
            VulkanSSAOPass& GetSSAOPass() const;
            VulkanRenderTarget& GetSceneRenderTarget(uint32_t frameIndex) const;
            VulkanSkyboxPass& GetSkyboxPass() const;
            VulkanDeferredLightingPass& GetDeferredLightingPass() const;
            VulkanAutoExposurePass& GetAutoExposurePass() const;
            VulkanBloomPass& GetBloomPass() const;
            VulkanFullscreenPass& GetFullscreenPass() const;
            VulkanRenderTarget& GetOutputRenderTarget(uint32_t frameIndex) const;
            VulkanRenderViewImages GetImages(uint32_t frameIndex) const;

        private:
            std::unique_ptr<VulkanGraphicsPipeline> CreateGBufferPipeline(VkDescriptorSetLayout globalDescriptorSetLayout, VkDescriptorSetLayout materialDescriptorSetLayout);

        private:
            VulkanDevice& m_Device;
            VkExtent2D m_Extent{};
            std::vector<std::unique_ptr<VulkanGBuffer>> m_GBuffers;
            std::unique_ptr<VulkanGraphicsPipeline> m_GBufferPipeline;
            std::unique_ptr<VulkanSSAOPass> m_SSAOPass;
            std::vector<std::unique_ptr<VulkanRenderTarget>> m_SceneRenderTargets;
            std::unique_ptr<VulkanSkyboxPass> m_SkyboxPass;
            std::unique_ptr<VulkanDeferredLightingPass> m_DeferredLightingPass;
            std::unique_ptr<VulkanAutoExposurePass> m_AutoExposurePass;
            std::unique_ptr<VulkanBloomPass> m_BloomPass;
            std::vector<std::unique_ptr<VulkanRenderTarget>> m_OutputRenderTargets;
            std::unique_ptr<VulkanFullscreenPass> m_FullscreenPass;
    };
}
