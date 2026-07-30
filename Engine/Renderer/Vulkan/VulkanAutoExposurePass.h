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

    class VulkanAutoExposurePass
    {
        public:
            VulkanAutoExposurePass(VulkanDevice& device, VkExtent2D extent, const std::vector<VkImageView>& sceneColorImageViews);
            ~VulkanAutoExposurePass();

            VulkanAutoExposurePass(const VulkanAutoExposurePass&) = delete;
            VulkanAutoExposurePass& operator=(const VulkanAutoExposurePass&) = delete;

            void Record(VkCommandBuffer commandBuffer, uint32_t frameIndex, float deltaTime, float increaseSpeed, float decreaseSpeed, float minimumExposure, float maximumExposure) const;
            VkImageView GetExposureImageView(uint32_t frameIndex) const;
            VkImageLayout GetLayout() const
            {
                return VK_IMAGE_LAYOUT_GENERAL;
            }

        private:
            struct FrameResources
            {
                std::unique_ptr<VulkanImage> statisticsImage;
                std::vector<VkImageView> statisticsMipImageViews;
                std::vector<VkDescriptorSet> reductionDescriptorSets;
                std::unique_ptr<VulkanImage> exposureImage;
                VkDescriptorSet adaptationDescriptorSet = VK_NULL_HANDLE;
            };

            VulkanDevice& m_Device;
            VkExtent2D m_Extent{};
            uint32_t m_MipLevels = 1;
            std::vector<FrameResources> m_Frames;
            std::unique_ptr<VulkanDescriptorSetLayout> m_ReductionDescriptorSetLayout;
            std::unique_ptr<VulkanDescriptorSetLayout> m_AdaptationDescriptorSetLayout;
            std::unique_ptr<VulkanDescriptorPool> m_DescriptorPool;
            std::unique_ptr<VulkanComputePipeline> m_ReductionPipeline;
            std::unique_ptr<VulkanComputePipeline> m_AdaptationPipeline;
    };
}
