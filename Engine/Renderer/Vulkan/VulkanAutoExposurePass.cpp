#include "Renderer/Vulkan/VulkanAutoExposurePass.h"
#include "Renderer/Vulkan/VulkanComputePipeline.h"
#include "Renderer/Vulkan/VulkanDescriptorPool.h"
#include "Renderer/Vulkan/VulkanDescriptorSetLayout.h"
#include "Renderer/Vulkan/VulkanDescriptorWriter.h"
#include "Renderer/Vulkan/VulkanDevice.h"
#include "Renderer/Vulkan/VulkanImage.h"

#include <algorithm>
#include <filesystem>
#include <stdexcept>

namespace
{
    constexpr VkFormat LuminanceStatisticsFormat = VK_FORMAT_R32G32_SFLOAT;
    constexpr VkFormat ExposureFormat = VK_FORMAT_R32_SFLOAT;
    constexpr uint32_t WorkgroupSize = 8;

    struct AutoExposurePushConstant
    {
        uint32_t firstPass;
    };

    struct ExposureAdaptPushConstant
    {
        float deltaTime;
        float increaseSpeed;
        float decreaseSpeed;
        float minimumExposure;
        float maximumExposure;
    };

    static_assert(sizeof(AutoExposurePushConstant) == 4);
    static_assert(sizeof(ExposureAdaptPushConstant) == 20);

    void InsertImageBarrier(VkCommandBuffer commandBuffer, VkImage image, uint32_t baseMipLevel, uint32_t levelCount, VkPipelineStageFlags sourceStage, VkPipelineStageFlags destinationStage, VkAccessFlags sourceAccess, VkAccessFlags destinationAccess)
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
    VulkanAutoExposurePass::VulkanAutoExposurePass(VulkanDevice& device, VkExtent2D extent, const std::vector<VkImageView>& sceneColorImageViews)
        : m_Device(device), m_Extent(extent)
    {
        if (extent.width == 0 || extent.height == 0 || sceneColorImageViews.size() < 2)
        {
            throw std::runtime_error("Exposure adaptation requires valid scene images and at least two frames in flight!");
        }

        for (VkImageView imageView : sceneColorImageViews)
        {
            if (imageView == VK_NULL_HANDLE)
            {
                throw std::runtime_error("Auto exposure pass contains a null scene color image view!");
            }
        }

        uint32_t mipWidth = extent.width;
        uint32_t mipHeight = extent.height;
        while (mipWidth > 1 || mipHeight > 1)
        {
            mipWidth = std::max(mipWidth / 2, 1u);
            mipHeight = std::max(mipHeight / 2, 1u);
            ++m_MipLevels;
        }

        VkFormatProperties statisticsProperties{};
        vkGetPhysicalDeviceFormatProperties(m_Device.GetPhysicalDevice(), LuminanceStatisticsFormat, &statisticsProperties);
        if ((statisticsProperties.optimalTilingFeatures & (VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) != (VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
        {
            throw std::runtime_error("Device does not support the luminance statistics format!");
        }

        VkFormatProperties exposureProperties{};
        vkGetPhysicalDeviceFormatProperties(m_Device.GetPhysicalDevice(), ExposureFormat, &exposureProperties);
        if ((exposureProperties.optimalTilingFeatures & (VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) != (VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
        {
            throw std::runtime_error("Device does not support the exposure image format!");
        }

        VkDescriptorSetLayoutBinding reductionSourceBinding{};
        reductionSourceBinding.binding = 0;
        reductionSourceBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        reductionSourceBinding.descriptorCount = 1;
        reductionSourceBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding reductionDestinationBinding{};
        reductionDestinationBinding.binding = 1;
        reductionDestinationBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        reductionDestinationBinding.descriptorCount = 1;
        reductionDestinationBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        m_ReductionDescriptorSetLayout = std::make_unique<VulkanDescriptorSetLayout>(m_Device, std::vector<VkDescriptorSetLayoutBinding>{reductionSourceBinding, reductionDestinationBinding});

        VkDescriptorSetLayoutBinding statisticsBinding{};
        statisticsBinding.binding = 0;
        statisticsBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        statisticsBinding.descriptorCount = 1;
        statisticsBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding previousExposureBinding{};
        previousExposureBinding.binding = 1;
        previousExposureBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        previousExposureBinding.descriptorCount = 1;
        previousExposureBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding currentExposureBinding{};
        currentExposureBinding.binding = 2;
        currentExposureBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        currentExposureBinding.descriptorCount = 1;
        currentExposureBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        m_AdaptationDescriptorSetLayout = std::make_unique<VulkanDescriptorSetLayout>(m_Device, std::vector<VkDescriptorSetLayoutBinding>{statisticsBinding, previousExposureBinding, currentExposureBinding});

        VkPushConstantRange reductionPushConstantRange{};
        reductionPushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        reductionPushConstantRange.size = sizeof(AutoExposurePushConstant);

        VkPushConstantRange adaptationPushConstantRange{};
        adaptationPushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        adaptationPushConstantRange.size = sizeof(ExposureAdaptPushConstant);

        m_ReductionPipeline = std::make_unique<VulkanComputePipeline>(m_Device, std::filesystem::path(KOSMOS_SHADER_DIR) / "AutoExposure.comp.spv", std::vector<VkDescriptorSetLayout>{m_ReductionDescriptorSetLayout->GetHandle()}, std::vector<VkPushConstantRange>{reductionPushConstantRange});
        m_AdaptationPipeline = std::make_unique<VulkanComputePipeline>(m_Device, std::filesystem::path(KOSMOS_SHADER_DIR) / "ExposureAdapt.comp.spv", std::vector<VkDescriptorSetLayout>{m_AdaptationDescriptorSetLayout->GetHandle()}, std::vector<VkPushConstantRange>{adaptationPushConstantRange});

        const uint32_t frameCount = static_cast<uint32_t>(sceneColorImageViews.size());
        const uint32_t reductionDescriptorSetCount = frameCount * m_MipLevels;
        const uint32_t descriptorSetCount = reductionDescriptorSetCount + frameCount;

        VkDescriptorPoolSize sampledImagePoolSize{};
        sampledImagePoolSize.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        sampledImagePoolSize.descriptorCount = reductionDescriptorSetCount + frameCount * 2;

        VkDescriptorPoolSize storageImagePoolSize{};
        storageImagePoolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        storageImagePoolSize.descriptorCount = reductionDescriptorSetCount + frameCount;

        m_DescriptorPool = std::make_unique<VulkanDescriptorPool>(m_Device, descriptorSetCount, std::vector<VkDescriptorPoolSize>{sampledImagePoolSize, storageImagePoolSize});
        m_Frames.resize(frameCount);
        VulkanDescriptorWriter writer(m_Device);

        for (uint32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
        {
            FrameResources& frame = m_Frames[frameIndex];
            frame.statisticsImage = std::make_unique<VulkanImage>(m_Device, extent.width, extent.height, LuminanceStatisticsFormat, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, m_MipLevels);
            frame.exposureImage = std::make_unique<VulkanImage>(m_Device, 1, 1, ExposureFormat, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
            m_Device.TransitionImageLayout(frame.statisticsImage->GetHandle(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, m_MipLevels);
            m_Device.TransitionImageLayout(frame.exposureImage->GetHandle(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

            frame.statisticsMipImageViews.reserve(m_MipLevels);
            for (uint32_t mipLevel = 0; mipLevel < m_MipLevels; ++mipLevel)
            {
                VkImageViewCreateInfo viewInfo{};
                viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                viewInfo.image = frame.statisticsImage->GetHandle();
                viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                viewInfo.format = LuminanceStatisticsFormat;
                viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                viewInfo.subresourceRange.baseMipLevel = mipLevel;
                viewInfo.subresourceRange.levelCount = 1;
                viewInfo.subresourceRange.baseArrayLayer = 0;
                viewInfo.subresourceRange.layerCount = 1;

                VkImageView imageView = VK_NULL_HANDLE;
                if (vkCreateImageView(m_Device.GetHandle(), &viewInfo, nullptr, &imageView) != VK_SUCCESS)
                {
                    throw std::runtime_error("Failed to create luminance statistics mip image view!");
                }

                frame.statisticsMipImageViews.push_back(imageView);
            }

            frame.reductionDescriptorSets = m_DescriptorPool->AllocateSets(m_ReductionDescriptorSetLayout->GetHandle(), m_MipLevels);
            for (uint32_t mipLevel = 0; mipLevel < m_MipLevels; ++mipLevel)
            {
                const VkImageView sourceView = mipLevel == 0 ? sceneColorImageViews[frameIndex] : frame.statisticsMipImageViews[mipLevel - 1];
                const VkImageLayout sourceLayout = mipLevel == 0 ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
                writer.WriteImage(frame.reductionDescriptorSets[mipLevel], 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, sourceView, VK_NULL_HANDLE, sourceLayout);
                writer.WriteImage(frame.reductionDescriptorSets[mipLevel], 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, frame.statisticsMipImageViews[mipLevel], VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL);
            }
        }

        m_Device.ExecuteSingleTimeCommands([&](VkCommandBuffer commandBuffer)
        {
            VkClearColorValue clearValue{};
            clearValue.float32[0] = 1.0f;

            VkImageSubresourceRange range{};
            range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            range.baseMipLevel = 0;
            range.levelCount = 1;
            range.baseArrayLayer = 0;
            range.layerCount = 1;

            for (FrameResources& frame : m_Frames)
            {
                vkCmdClearColorImage(commandBuffer, frame.exposureImage->GetHandle(), VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &range);
            }
        });

        for (uint32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
        {
            FrameResources& frame = m_Frames[frameIndex];
            const uint32_t previousFrameIndex = (frameIndex + frameCount - 1) % frameCount;
            frame.adaptationDescriptorSet = m_DescriptorPool->AllocateSets(m_AdaptationDescriptorSetLayout->GetHandle(), 1).front();
            writer.WriteImage(frame.adaptationDescriptorSet, 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, frame.statisticsMipImageViews.back(), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL);
            writer.WriteImage(frame.adaptationDescriptorSet, 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, m_Frames[previousFrameIndex].exposureImage->GetImageView(), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL);
            writer.WriteImage(frame.adaptationDescriptorSet, 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, frame.exposureImage->GetImageView(), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL);
        }
    }

    VulkanAutoExposurePass::~VulkanAutoExposurePass()
    {
        m_ReductionPipeline.reset();
        m_AdaptationPipeline.reset();
        m_DescriptorPool.reset();
        m_ReductionDescriptorSetLayout.reset();
        m_AdaptationDescriptorSetLayout.reset();

        for (FrameResources& frame : m_Frames)
        {
            for (VkImageView imageView : frame.statisticsMipImageViews)
            {
                if (imageView != VK_NULL_HANDLE)
                {
                    vkDestroyImageView(m_Device.GetHandle(), imageView, nullptr);
                }
            }
        }
    }

    void VulkanAutoExposurePass::Record(VkCommandBuffer commandBuffer, uint32_t frameIndex, float deltaTime, float increaseSpeed, float decreaseSpeed, float minimumExposure, float maximumExposure) const
    {
        const FrameResources& frame = m_Frames.at(frameIndex);
        const uint32_t previousFrameIndex = (frameIndex + static_cast<uint32_t>(m_Frames.size()) - 1) % static_cast<uint32_t>(m_Frames.size());
        const FrameResources& previousFrame = m_Frames[previousFrameIndex];
        const VkImage statisticsImage = frame.statisticsImage->GetHandle();

        InsertImageBarrier(commandBuffer, statisticsImage, 0, m_MipLevels, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_ReductionPipeline->GetHandle());

        for (uint32_t mipLevel = 0; mipLevel < m_MipLevels; ++mipLevel)
        {
            const uint32_t width = std::max(m_Extent.width >> mipLevel, 1u);
            const uint32_t height = std::max(m_Extent.height >> mipLevel, 1u);
            const VkDescriptorSet descriptorSet = frame.reductionDescriptorSets[mipLevel];
            const AutoExposurePushConstant pushConstant{mipLevel == 0 ? 1u : 0u};
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_ReductionPipeline->GetLayout(), 0, 1, &descriptorSet, 0, nullptr);
            vkCmdPushConstants(commandBuffer, m_ReductionPipeline->GetLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(AutoExposurePushConstant), &pushConstant);
            vkCmdDispatch(commandBuffer, (width + WorkgroupSize - 1) / WorkgroupSize, (height + WorkgroupSize - 1) / WorkgroupSize, 1);
            InsertImageBarrier(commandBuffer, statisticsImage, mipLevel, 1, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        }

        InsertImageBarrier(commandBuffer, previousFrame.exposureImage->GetHandle(), 0, 1, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        InsertImageBarrier(commandBuffer, frame.exposureImage->GetHandle(), 0, 1, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT);

        ExposureAdaptPushConstant pushConstant{};
        pushConstant.deltaTime = std::clamp(deltaTime, 0.0f, 0.1f);
        pushConstant.increaseSpeed = std::max(increaseSpeed, 0.0f);
        pushConstant.decreaseSpeed = std::max(decreaseSpeed, 0.0f);
        pushConstant.minimumExposure = std::max(minimumExposure, 0.0001f);
        pushConstant.maximumExposure = std::max(maximumExposure, pushConstant.minimumExposure);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_AdaptationPipeline->GetHandle());
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_AdaptationPipeline->GetLayout(), 0, 1, &frame.adaptationDescriptorSet, 0, nullptr);
        vkCmdPushConstants(commandBuffer, m_AdaptationPipeline->GetLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ExposureAdaptPushConstant), &pushConstant);
        vkCmdDispatch(commandBuffer, 1, 1, 1);
        InsertImageBarrier(commandBuffer, frame.exposureImage->GetHandle(), 0, 1, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

    VkImageView VulkanAutoExposurePass::GetExposureImageView(uint32_t frameIndex) const
    {
        return m_Frames.at(frameIndex).exposureImage->GetImageView();
    }
}
