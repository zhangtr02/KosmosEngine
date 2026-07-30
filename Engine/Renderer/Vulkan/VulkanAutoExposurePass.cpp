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
    constexpr uint32_t WorkgroupSize = 8;

    struct AutoExposurePushConstant
    {
        uint32_t firstPass;
    };

    static_assert(sizeof(AutoExposurePushConstant) == 4);

    void InsertAutoExposureBarrier(VkCommandBuffer commandBuffer, VkImage image, uint32_t baseMipLevel, uint32_t levelCount, VkPipelineStageFlags sourceStage, VkPipelineStageFlags destinationStage, VkAccessFlags sourceAccess, VkAccessFlags destinationAccess)
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
        if (extent.width == 0 || extent.height == 0 || sceneColorImageViews.empty()) throw std::runtime_error("Auto exposure pass requires a valid extent and scene color images!");
        for (VkImageView imageView : sceneColorImageViews) if (imageView == VK_NULL_HANDLE) throw std::runtime_error("Auto exposure pass contains a null scene color image view!");

        uint32_t mipWidth = extent.width;
        uint32_t mipHeight = extent.height;
        while (mipWidth > 1 || mipHeight > 1)
        {
            mipWidth = std::max(mipWidth / 2, 1u);
            mipHeight = std::max(mipHeight / 2, 1u);
            ++m_MipLevels;
        }

        VkFormatProperties formatProperties{};
        vkGetPhysicalDeviceFormatProperties(m_Device.GetPhysicalDevice(), LuminanceStatisticsFormat, &formatProperties);
        const VkFormatFeatureFlags requiredFeatures = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if ((formatProperties.optimalTilingFeatures & requiredFeatures) != requiredFeatures) throw std::runtime_error("Device does not support the auto exposure image format!");

        VkDescriptorSetLayoutBinding sourceBinding{};
        sourceBinding.binding = 0;
        sourceBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        sourceBinding.descriptorCount = 1;
        sourceBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutBinding destinationBinding{};
        destinationBinding.binding = 1;
        destinationBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        destinationBinding.descriptorCount = 1;
        destinationBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        m_DescriptorSetLayout = std::make_unique<VulkanDescriptorSetLayout>(m_Device, std::vector<VkDescriptorSetLayoutBinding>{sourceBinding, destinationBinding});

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(AutoExposurePushConstant);
        m_Pipeline = std::make_unique<VulkanComputePipeline>(m_Device, std::filesystem::path(KOSMOS_SHADER_DIR) / "AutoExposure.comp.spv", std::vector<VkDescriptorSetLayout>{m_DescriptorSetLayout->GetHandle()}, std::vector<VkPushConstantRange>{pushConstantRange});

        const uint32_t descriptorSetCount = static_cast<uint32_t>(sceneColorImageViews.size()) * m_MipLevels;

        VkDescriptorPoolSize sampledImagePoolSize{};
        sampledImagePoolSize.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        sampledImagePoolSize.descriptorCount = descriptorSetCount;

        VkDescriptorPoolSize storageImagePoolSize{};
        storageImagePoolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        storageImagePoolSize.descriptorCount = descriptorSetCount;

        m_DescriptorPool = std::make_unique<VulkanDescriptorPool>(m_Device, descriptorSetCount, std::vector<VkDescriptorPoolSize>{sampledImagePoolSize, storageImagePoolSize});
        m_Frames.resize(sceneColorImageViews.size());
        VulkanDescriptorWriter writer(m_Device);

        for (uint32_t frameIndex = 0; frameIndex < static_cast<uint32_t>(m_Frames.size()); ++frameIndex)
        {
            FrameResources& frame = m_Frames[frameIndex];
            frame.image = std::make_unique<VulkanImage>(m_Device, extent.width, extent.height, LuminanceStatisticsFormat, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, m_MipLevels);
            m_Device.TransitionImageLayout(frame.image->GetHandle(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, m_MipLevels);
            frame.mipImageViews.reserve(m_MipLevels);

            for (uint32_t mipLevel = 0; mipLevel < m_MipLevels; ++mipLevel)
            {
                VkImageViewCreateInfo viewInfo{};
                viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                viewInfo.image = frame.image->GetHandle();
                viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                viewInfo.format = LuminanceStatisticsFormat;
                viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                viewInfo.subresourceRange.baseMipLevel = mipLevel;
                viewInfo.subresourceRange.levelCount = 1;
                viewInfo.subresourceRange.baseArrayLayer = 0;
                viewInfo.subresourceRange.layerCount = 1;

                VkImageView imageView = VK_NULL_HANDLE;
                if (vkCreateImageView(m_Device.GetHandle(), &viewInfo, nullptr, &imageView) != VK_SUCCESS) throw std::runtime_error("Failed to create auto exposure mip image view!");
                frame.mipImageViews.push_back(imageView);
            }

            frame.descriptorSets = m_DescriptorPool->AllocateSets(m_DescriptorSetLayout->GetHandle(), m_MipLevels);

            for (uint32_t mipLevel = 0; mipLevel < m_MipLevels; ++mipLevel)
            {
                const VkImageView sourceView = mipLevel == 0 ? sceneColorImageViews[frameIndex] : frame.mipImageViews[mipLevel - 1];
                const VkImageLayout sourceLayout = mipLevel == 0 ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
                writer.WriteImage(frame.descriptorSets[mipLevel], 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, sourceView, VK_NULL_HANDLE, sourceLayout);
                writer.WriteImage(frame.descriptorSets[mipLevel], 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, frame.mipImageViews[mipLevel], VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL);
            }
        }
    }

    VulkanAutoExposurePass::~VulkanAutoExposurePass()
    {
        m_Pipeline.reset();
        m_DescriptorPool.reset();
        m_DescriptorSetLayout.reset();
        for (FrameResources& frame : m_Frames) for (VkImageView imageView : frame.mipImageViews) if (imageView != VK_NULL_HANDLE) vkDestroyImageView(m_Device.GetHandle(), imageView, nullptr);
    }

    void VulkanAutoExposurePass::Record(VkCommandBuffer commandBuffer, uint32_t frameIndex) const
    {
        const FrameResources& frame = m_Frames.at(frameIndex);
        const VkImage image = frame.image->GetHandle();

        InsertAutoExposureBarrier(commandBuffer, image, 0, m_MipLevels, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_Pipeline->GetHandle());

        for (uint32_t mipLevel = 0; mipLevel < m_MipLevels; ++mipLevel)
        {
            const uint32_t width = std::max(m_Extent.width >> mipLevel, 1u);
            const uint32_t height = std::max(m_Extent.height >> mipLevel, 1u);
            const VkDescriptorSet descriptorSet = frame.descriptorSets[mipLevel];
            const AutoExposurePushConstant pushConstant{mipLevel == 0 ? 1u : 0u};

            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_Pipeline->GetLayout(), 0, 1, &descriptorSet, 0, nullptr);
            vkCmdPushConstants(commandBuffer, m_Pipeline->GetLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(AutoExposurePushConstant), &pushConstant);
            vkCmdDispatch(commandBuffer, (width + WorkgroupSize - 1) / WorkgroupSize, (height + WorkgroupSize - 1) / WorkgroupSize, 1);

            if (mipLevel + 1 < m_MipLevels) InsertAutoExposureBarrier(commandBuffer, image, mipLevel, 1, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
            else InsertAutoExposureBarrier(commandBuffer, image, mipLevel, 1, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        }
    }

    VkImageView VulkanAutoExposurePass::GetLuminanceStatisticsImageView(uint32_t frameIndex) const
    {
        return m_Frames.at(frameIndex).mipImageViews.back();
    }
}