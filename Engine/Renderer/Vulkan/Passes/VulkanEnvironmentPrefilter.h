#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

namespace Kosmos
{
    class VulkanDevice;
    class VulkanImage;
    class VulkanCubeTexture;
    class VulkanDescriptorSetLayout;
    class VulkanDescriptorPool;
    class VulkanComputePipeline;

    class VulkanEnvironmentPrefilter
    {
        public:
            VulkanEnvironmentPrefilter(VulkanDevice& device, const VulkanCubeTexture& source, uint32_t sourceResolution, uint32_t resolution, uint32_t sampleCount);
            ~VulkanEnvironmentPrefilter();

            VulkanEnvironmentPrefilter(const VulkanEnvironmentPrefilter&) = delete;
            VulkanEnvironmentPrefilter& operator=(const VulkanEnvironmentPrefilter&) = delete;

            VkImageView GetImageView() const;
            VkSampler GetSampler() const { return m_Sampler; }
            VkImageLayout GetLayout() const { return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; }
            uint32_t GetMipLevels() const { return m_MipLevels; }

        private:
            VulkanDevice& m_Device;
            std::unique_ptr<VulkanImage> m_Image;
            VkSampler m_Sampler = VK_NULL_HANDLE;
            std::vector<VkImageView> m_MipImageViews;
            std::unique_ptr<VulkanDescriptorSetLayout> m_DescriptorSetLayout;
            std::unique_ptr<VulkanDescriptorPool> m_DescriptorPool;
            std::vector<VkDescriptorSet> m_DescriptorSets;
            std::unique_ptr<VulkanComputePipeline> m_Pipeline;
            uint32_t m_Resolution = 0;
            uint32_t m_MipLevels = 1;
    };
}