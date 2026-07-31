#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

namespace Kosmos
{
    class VulkanDevice;
    class VulkanImage;
    class VulkanDescriptorSetLayout;
    class VulkanDescriptorPool;
    class VulkanComputePipeline;

    class VulkanBloomPass
    {
        public:
            VulkanBloomPass(VulkanDevice& device, VkExtent2D sceneExtent, const std::vector<VkImageView>& sceneColorImageViews);
            ~VulkanBloomPass();

            VulkanBloomPass(const VulkanBloomPass&) = delete;
            VulkanBloomPass& operator=(const VulkanBloomPass&) = delete;

            void Record(VkCommandBuffer commandBuffer, uint32_t frameIndex, float threshold, float knee) const;
            VkImageView GetBloomImageView(uint32_t frameIndex) const;
            VkImageLayout GetLayout() const { return VK_IMAGE_LAYOUT_GENERAL; }

        private:
            struct FrameResources
            {
                std::unique_ptr<VulkanImage> image;
                std::vector<VkImageView> mipImageViews;
                std::vector<VkDescriptorSet> downsampleDescriptorSets;
                std::vector<VkDescriptorSet> upsampleDescriptorSets;
            };

            VulkanDevice& m_Device;
            VkExtent2D m_Extent{};
            uint32_t m_MipLevels = 1;
            std::vector<FrameResources> m_Frames;
            std::unique_ptr<VulkanDescriptorSetLayout> m_DescriptorSetLayout;
            std::unique_ptr<VulkanDescriptorPool> m_DescriptorPool;
            std::unique_ptr<VulkanComputePipeline> m_DownsamplePipeline;
            std::unique_ptr<VulkanComputePipeline> m_UpsamplePipeline;
            VkSampler m_Sampler = VK_NULL_HANDLE;
    };
}