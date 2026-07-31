#include "Renderer/Vulkan/Passes/VulkanEnvironmentPrefilter.h"
#include "Renderer/Vulkan/Pipelines/VulkanComputePipeline.h"
#include "Renderer/Vulkan/Resources/VulkanCubeTexture.h"
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
    constexpr VkFormat PrefilterFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    constexpr uint32_t WorkgroupSize = 8;

    struct EnvironmentPrefilterPushConstant
    {
        float roughness;
        float sourceResolution;
        uint32_t sampleCount;
        float maxSourceLod;
    };

    static_assert(sizeof(EnvironmentPrefilterPushConstant) == 16);
}

namespace Kosmos
{
    VulkanEnvironmentPrefilter::VulkanEnvironmentPrefilter(VulkanDevice& device, const VulkanCubeTexture& source, uint32_t sourceResolution, uint32_t resolution, uint32_t sampleCount)
        : m_Device(device), m_Resolution(resolution)
    {
        if (sourceResolution == 0 || resolution == 0 || sampleCount == 0)
        {
            throw std::runtime_error("Environment prefilter dimensions and sample count must be greater than zero!");
        }

        if ((resolution & (resolution - 1)) != 0)
        {
            throw std::runtime_error("Environment prefilter resolution must be a power of two!");
        }

        for (uint32_t mipSize = resolution; mipSize > 1; mipSize /= 2)
        {
            ++m_MipLevels;
        }

        VkFormatProperties formatProperties{};
        vkGetPhysicalDeviceFormatProperties(m_Device.GetPhysicalDevice(), PrefilterFormat, &formatProperties);
        const VkFormatFeatureFlags requiredFeatures = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

        if ((formatProperties.optimalTilingFeatures & requiredFeatures) != requiredFeatures)
        {
            throw std::runtime_error("Device does not support the prefiltered environment image format!");
        }

        m_Image = std::make_unique<VulkanImage>(m_Device, resolution, resolution, PrefilterFormat, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, m_MipLevels, 6, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, VK_IMAGE_VIEW_TYPE_CUBE);
        m_Device.TransitionImageLayout(m_Image->GetHandle(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, m_MipLevels, 6);

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

        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(EnvironmentPrefilterPushConstant);

        m_Pipeline = std::make_unique<VulkanComputePipeline>(m_Device, std::filesystem::path(KOSMOS_SHADER_DIR) / "EnvironmentPrefilter.comp.spv", std::vector<VkDescriptorSetLayout>{m_DescriptorSetLayout->GetHandle()}, std::vector<VkPushConstantRange>{pushConstantRange});

        VkDescriptorPoolSize sourcePoolSize{};
        sourcePoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sourcePoolSize.descriptorCount = m_MipLevels;

        VkDescriptorPoolSize destinationPoolSize{};
        destinationPoolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        destinationPoolSize.descriptorCount = m_MipLevels;

        m_DescriptorPool = std::make_unique<VulkanDescriptorPool>(m_Device, m_MipLevels, std::vector<VkDescriptorPoolSize>{sourcePoolSize, destinationPoolSize});
        m_DescriptorSets = m_DescriptorPool->AllocateSets(m_DescriptorSetLayout->GetHandle(), m_MipLevels);
        m_MipImageViews.reserve(m_MipLevels);

        for (uint32_t mipLevel = 0; mipLevel < m_MipLevels; ++mipLevel)
        {
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = m_Image->GetHandle();
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            viewInfo.format = PrefilterFormat;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = mipLevel;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 6;

            VkImageView imageView = VK_NULL_HANDLE;

            if (vkCreateImageView(m_Device.GetHandle(), &viewInfo, nullptr, &imageView) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create environment prefilter mip image view!");
            }

            m_MipImageViews.push_back(imageView);
        }

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = static_cast<float>(m_MipLevels - 1);

        if (vkCreateSampler(m_Device.GetHandle(), &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create prefiltered environment sampler!");
        }

        VulkanDescriptorWriter writer(m_Device);

        for (uint32_t mipLevel = 0; mipLevel < m_MipLevels; ++mipLevel)
        {
            writer.WriteImage(m_DescriptorSets[mipLevel], 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, source.GetImageView(), source.GetSampler(), source.GetLayout());
            writer.WriteImage(m_DescriptorSets[mipLevel], 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_MipImageViews[mipLevel], VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL);
        }

        m_Device.ExecuteSingleTimeCommands([&](VkCommandBuffer commandBuffer)
        {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_Pipeline->GetHandle());

            for (uint32_t mipLevel = 0; mipLevel < m_MipLevels; ++mipLevel)
            {
                const uint32_t mipResolution = std::max(m_Resolution >> mipLevel, 1u);

                EnvironmentPrefilterPushConstant pushConstant{};
                pushConstant.roughness = m_MipLevels > 1 ? static_cast<float>(mipLevel) / static_cast<float>(m_MipLevels - 1) : 0.0f;
                pushConstant.sourceResolution = static_cast<float>(sourceResolution);
                pushConstant.sampleCount = sampleCount;
                pushConstant.maxSourceLod = static_cast<float>(source.GetMipLevels() - 1);

                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_Pipeline->GetLayout(), 0, 1, &m_DescriptorSets[mipLevel], 0, nullptr);
                vkCmdPushConstants(commandBuffer, m_Pipeline->GetLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(EnvironmentPrefilterPushConstant), &pushConstant);
                vkCmdDispatch(commandBuffer, (mipResolution + WorkgroupSize - 1) / WorkgroupSize, (mipResolution + WorkgroupSize - 1) / WorkgroupSize, 6);
            }
        });

        m_Device.TransitionImageLayout(m_Image->GetHandle(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_MipLevels, 6);
    }

    VulkanEnvironmentPrefilter::~VulkanEnvironmentPrefilter()
    {
        m_Pipeline.reset();
        m_DescriptorSets.clear();
        m_DescriptorPool.reset();
        m_DescriptorSetLayout.reset();

        for (VkImageView imageView : m_MipImageViews)
        {
            if (imageView != VK_NULL_HANDLE)
            {
                vkDestroyImageView(m_Device.GetHandle(), imageView, nullptr);
            }
        }

        if (m_Sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(m_Device.GetHandle(), m_Sampler, nullptr);
        }
    }

    VkImageView VulkanEnvironmentPrefilter::GetImageView() const
    {
        return m_Image->GetImageView();
    }
}
