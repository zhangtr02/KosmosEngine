#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Kosmos
{
    class Mesh;
    class Scene;
    class VulkanDevice;
    class VulkanMesh;
    class VulkanRenderTarget;
    class VulkanGraphicsPipeline;

    class VulkanDirectionalShadowPass
    {
        public:
            VulkanDirectionalShadowPass(VulkanDevice& device, const Scene& scene, const std::unordered_map<const Mesh*, std::unique_ptr<VulkanMesh>>& meshes, VkDescriptorSetLayout globalDescriptorSetLayout, uint32_t frameCount);
            ~VulkanDirectionalShadowPass();

            VulkanDirectionalShadowPass(const VulkanDirectionalShadowPass&) = delete;
            VulkanDirectionalShadowPass& operator=(const VulkanDirectionalShadowPass&) = delete;

            void Record(VkCommandBuffer commandBuffer, uint32_t frameIndex, VkDescriptorSet globalDescriptorSet) const;

            VkImageView GetShadowMapImageView(uint32_t frameIndex) const;
            VkSampler GetSampler() const { return m_Sampler; }

        private:
            VulkanDevice& m_Device;
            const Scene& m_Scene;
            const std::unordered_map<const Mesh*, std::unique_ptr<VulkanMesh>>& m_Meshes;

            std::vector<std::unique_ptr<VulkanRenderTarget>> m_RenderTargets;
            std::unique_ptr<VulkanGraphicsPipeline> m_Pipeline;
            VkSampler m_Sampler = VK_NULL_HANDLE;
    };
}