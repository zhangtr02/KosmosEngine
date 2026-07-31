#pragma once

#include <vulkan/vulkan.h>

namespace Kosmos
{
    struct VulkanRenderViewImages
    {
        VkImageView finalColor = VK_NULL_HANDLE;
        VkImageView albedoAmbientOcclusion = VK_NULL_HANDLE;
        VkImageView normalRoughness = VK_NULL_HANDLE;
        VkImageView materialParameters = VK_NULL_HANDLE;
        VkImageView depth = VK_NULL_HANDLE;
        VkImageView rawAmbientOcclusion = VK_NULL_HANDLE;
        VkImageView ambientOcclusion = VK_NULL_HANDLE;
        VkImageView sceneColor = VK_NULL_HANDLE;
        VkImageView bloom = VK_NULL_HANDLE;
    };
}
