#include "Renderer/Vulkan/Passes/VulkanBloomPass.h"
#include "Renderer/Vulkan/Pipelines/VulkanComputePipeline.h"
#include "Renderer/Vulkan/Descriptors/VulkanDescriptorPool.h"
#include "Renderer/Vulkan/Descriptors/VulkanDescriptorSetLayout.h"
#include "Renderer/Vulkan/Descriptors/VulkanDescriptorWriter.h"
#include "Renderer/Vulkan/Core/VulkanDevice.h"
#include "Renderer/Vulkan/Resources/VulkanImage.h"

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace
{
    constexpr VkFormat BloomFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    constexpr uint32_t MaximumBloomMipLevels = 6;
    constexpr uint32_t WorkgroupSize = 8;

    struct BloomDownsamplePushConstant
    {
        float threshold;
        float knee;
        uint32_t applyThreshold;
    };

    static_assert(sizeof(BloomDownsamplePushConstant) == 12);

    void InsertBloomBarrier(VkCommandBuffer commandBuffer, VkImage image, uint32_t baseMipLevel, uint32_t levelCount, VkPipelineStageFlags sourceStage, VkPipelineStageFlags destinationStage, VkAccessFlags sourceAccess, VkAccessFlags destinationAccess)
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = sourceAccess;
        barrier.dstAccessMask = destinationAccess;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = baseMipLevel;
        barrier.subresourceRange.levelCount = levelCount;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
}

namespace Kosmos
{
    VulkanBloomPass::VulkanBloomPass(VulkanDevice& device, VkExtent2D sceneExtent, const std::vector<VkImageView>& sceneColorImageViews)
        : m_Device(device)
    {
        if (sceneExtent.width == 0 || sceneExtent.height == 0 || sceneColorImageViews.empty())
        {
            throw std::runtime_error("Bloom pass requires a valid extent and scene color images!");
        }

        for (VkImageView imageView : sceneColorImageViews)
        {
            if (imageView == VK_NULL_HANDLE)
            {
                throw std::runtime_error("Bloom pass contains a null scene color image view!");
            }
        }

        m_Extent.width = std::max(sceneExtent.width / 2, 1u);
        m_Extent.height = std::max(sceneExtent.height / 2, 1u);

        uint32_t mipWidth = m_Extent.width;
        uint32_t mipHeight = m_Extent.height;

        while (m_MipLevels < MaximumBloomMipLevels && (mipWidth > 1 || mipHeight > 1))
        {
            mipWidth = std::max(mipWidth / 2, 1u);
            mipHeight = std::max(mipHeight / 2, 1u);
            ++m_MipLevels;
        }

        VkFormatProperties formatProperties{};
        vkGetPhysicalDeviceFormatProperties(m_Device.GetPhysicalDevice(), BloomFormat, &formatProperties);
        const VkFormatFeatureFlags requiredFeatures = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

        if ((formatProperties.optimalTilingFeatures & requiredFeatures) != requiredFeatures)
        {
            throw std::runtime_error("Device does not support the bloom image format!");
        }

        VkDescriptorSetLayoutBinding sourceBinding{};
        sourceBinding.binding = 0;
        sourceBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sourceBinding.descriptorCount = 1;
        sourceBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding destinationBinding{};
        destinationBinding.binding = 1;
        destinationBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        destinationBinding.descriptorCount = 1;
        destinationBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        m_DescriptorSetLayout = std::make_unique<VulkanDescriptorSetLayout>(m_Device, std::vector<VkDescriptorSetLayoutBinding>{sourceBinding, destinationBinding});

        VkPushConstantRange downsamplePushConstantRange{};
        downsamplePushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        downsamplePushConstantRange.offset = 0;
        downsamplePushConstantRange.size = sizeof(BloomDownsamplePushConstant);

        m_DownsamplePipeline = std::make_unique<VulkanComputePipeline>(m_Device, std::filesystem::path(KOSMOS_SHADER_DIR) / "BloomDownsample.comp.spv", std::vector<VkDescriptorSetLayout>{m_DescriptorSetLayout->GetHandle()}, std::vector<VkPushConstantRange>{downsamplePushConstantRange});
        m_UpsamplePipeline = std::make_unique<VulkanComputePipeline>(m_Device, std::filesystem::path(KOSMOS_SHADER_DIR) / "BloomUpsample.comp.spv", std::vector<VkDescriptorSetLayout>{m_DescriptorSetLayout->GetHandle()});

        const uint32_t descriptorSetsPerFrame = m_MipLevels + m_MipLevels - 1;
        const uint32_t descriptorSetCount = static_cast<uint32_t>(sceneColorImageViews.size()) * descriptorSetsPerFrame;

        VkDescriptorPoolSize sampledImagePoolSize{};
        sampledImagePoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sampledImagePoolSize.descriptorCount = descriptorSetCount;

        VkDescriptorPoolSize storageImagePoolSize{};
        storageImagePoolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        storageImagePoolSize.descriptorCount = descriptorSetCount;

        m_DescriptorPool = std::make_unique<VulkanDescriptorPool>(m_Device, descriptorSetCount, std::vector<VkDescriptorPoolSize>{sampledImagePoolSize, storageImagePoolSize});

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;

        if (vkCreateSampler(m_Device.GetHandle(), &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create bloom sampler!");
        }

        m_Frames.resize(sceneColorImageViews.size());
        VulkanDescriptorWriter writer(m_Device);

        for (uint32_t frameIndex = 0; frameIndex < static_cast<uint32_t>(m_Frames.size()); ++frameIndex)
        {
            FrameResources& frame = m_Frames[frameIndex];
            frame.image = std::make_unique<VulkanImage>(m_Device, m_Extent.width, m_Extent.height, BloomFormat, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, m_MipLevels);
            m_Device.TransitionImageLayout(frame.image->GetHandle(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, m_MipLevels);
            frame.mipImageViews.reserve(m_MipLevels);

            for (uint32_t mipLevel = 0; mipLevel < m_MipLevels; ++mipLevel)
            {
                VkImageViewCreateInfo viewInfo{};
                viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                viewInfo.image = frame.image->GetHandle();
                viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                viewInfo.format = BloomFormat;
                viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                viewInfo.subresourceRange.baseMipLevel = mipLevel;
                viewInfo.subresourceRange.levelCount = 1;
                viewInfo.subresourceRange.baseArrayLayer = 0;
                viewInfo.subresourceRange.layerCount = 1;

                VkImageView imageView = VK_NULL_HANDLE;
                if (vkCreateImageView(m_Device.GetHandle(), &viewInfo, nullptr, &imageView) != VK_SUCCESS)
                {
                    throw std::runtime_error("Failed to create bloom mip image view!");
                }

                frame.mipImageViews.push_back(imageView);
            }

            frame.downsampleDescriptorSets = m_DescriptorPool->AllocateSets(m_DescriptorSetLayout->GetHandle(), m_MipLevels);
            if (m_MipLevels > 1)
            {
                frame.upsampleDescriptorSets = m_DescriptorPool->AllocateSets(m_DescriptorSetLayout->GetHandle(), m_MipLevels - 1);
            }

            for (uint32_t mipLevel = 0; mipLevel < m_MipLevels; ++mipLevel)
            {
                const VkImageView sourceView = mipLevel == 0 ? sceneColorImageViews[frameIndex] : frame.mipImageViews[mipLevel - 1];
                const VkImageLayout sourceLayout = mipLevel == 0 ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
                writer.WriteImage(frame.downsampleDescriptorSets[mipLevel], 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sourceView, m_Sampler, sourceLayout);
                writer.WriteImage(frame.downsampleDescriptorSets[mipLevel], 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, frame.mipImageViews[mipLevel], VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL);
            }

            for (uint32_t targetMipLevel = 0; targetMipLevel + 1 < m_MipLevels; ++targetMipLevel)
            {
                writer.WriteImage(frame.upsampleDescriptorSets[targetMipLevel], 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, frame.mipImageViews[targetMipLevel + 1], m_Sampler, VK_IMAGE_LAYOUT_GENERAL);
                writer.WriteImage(frame.upsampleDescriptorSets[targetMipLevel], 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, frame.mipImageViews[targetMipLevel], VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL);
            }
        }
    }

    VulkanBloomPass::~VulkanBloomPass()
    {
        m_DownsamplePipeline.reset();
        m_UpsamplePipeline.reset();
        m_DescriptorPool.reset();
        m_DescriptorSetLayout.reset();

        for (FrameResources& frame : m_Frames)
        {
            for (VkImageView imageView : frame.mipImageViews)
            {
                if (imageView != VK_NULL_HANDLE)
                {
                    vkDestroyImageView(m_Device.GetHandle(), imageView, nullptr);
                }
            }
        }

        if (m_Sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(m_Device.GetHandle(), m_Sampler, nullptr);
        }
    }

    void VulkanBloomPass::Record(VkCommandBuffer commandBuffer, uint32_t frameIndex, float threshold, float knee) const
    {
        const FrameResources& frame = m_Frames.at(frameIndex);
        const VkImage image = frame.image->GetHandle();

        InsertBloomBarrier(commandBuffer, image, 0, m_MipLevels, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_DownsamplePipeline->GetHandle());

        for (uint32_t mipLevel = 0; mipLevel < m_MipLevels; ++mipLevel)
        {
            BloomDownsamplePushConstant pushConstant{};
            pushConstant.threshold = std::max(threshold, 0.0f);
            pushConstant.knee = std::max(knee, 0.0001f);
            pushConstant.applyThreshold = mipLevel == 0 ? 1u : 0u;

            const uint32_t width = std::max(m_Extent.width >> mipLevel, 1u);
            const uint32_t height = std::max(m_Extent.height >> mipLevel, 1u);
            const VkDescriptorSet descriptorSet = frame.downsampleDescriptorSets[mipLevel];

            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_DownsamplePipeline->GetLayout(), 0, 1, &descriptorSet, 0, nullptr);
            vkCmdPushConstants(commandBuffer, m_DownsamplePipeline->GetLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(BloomDownsamplePushConstant), &pushConstant);
            vkCmdDispatch(commandBuffer, (width + WorkgroupSize - 1) / WorkgroupSize, (height + WorkgroupSize - 1) / WorkgroupSize, 1);
            InsertBloomBarrier(commandBuffer, image, mipLevel, 1, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        }

        if (m_MipLevels > 1)
        {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_UpsamplePipeline->GetHandle());

            for (uint32_t sourceMipLevel = m_MipLevels - 1; sourceMipLevel > 0; --sourceMipLevel)
            {
                const uint32_t targetMipLevel = sourceMipLevel - 1;
                const uint32_t width = std::max(m_Extent.width >> targetMipLevel, 1u);
                const uint32_t height = std::max(m_Extent.height >> targetMipLevel, 1u);
                const VkDescriptorSet descriptorSet = frame.upsampleDescriptorSets[targetMipLevel];

                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_UpsamplePipeline->GetLayout(), 0, 1, &descriptorSet, 0, nullptr);
                vkCmdDispatch(commandBuffer, (width + WorkgroupSize - 1) / WorkgroupSize, (height + WorkgroupSize - 1) / WorkgroupSize, 1);

                if (targetMipLevel > 0)
                {
                    InsertBloomBarrier(commandBuffer, image, targetMipLevel, 1, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
                }
            }
        }

        InsertBloomBarrier(commandBuffer, image, 0, 1, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

    VkImageView VulkanBloomPass::GetBloomImageView(uint32_t frameIndex) const
    {
        return m_Frames.at(frameIndex).mipImageViews.front();
    }
}
