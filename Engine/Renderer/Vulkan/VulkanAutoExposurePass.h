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

            void Record(VkCommandBuffer commandBuffer, uint32_t frameIndex) const;
            VkImageView GetLuminanceStatisticsImageView(uint32_t frameIndex) const;
            VkImageLayout GetLayout() const { return VK_IMAGE_LAYOUT_GENERAL; }

        private:
            struct FrameResources
            {
                std::unique_ptr<VulkanImage> image;
                std::vector<VkImageView> mipImageViews;
                std::vector<VkDescriptorSet> descriptorSets;
            };

            VulkanDevice& m_Device;
            VkExtent2D m_Extent{};
            uint32_t m_MipLevels = 1;
            std::vector<FrameResources> m_Frames;
            std::unique_ptr<VulkanDescriptorSetLayout> m_DescriptorSetLayout;
            std::unique_ptr<VulkanDescriptorPool> m_DescriptorPool;
            std::unique_ptr<VulkanComputePipeline> m_Pipeline;
    };
}