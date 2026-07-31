#pragma once

#include <vulkan/vulkan.h>
#include <optional>
#include <vector>

namespace Kosmos
{
    struct VulkanRenderTargetColorAttachmentDescription
    {
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageLayout finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    };

    struct VulkanRenderTargetDepthAttachmentDescription
    {
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageLayout finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    };

    struct VulkanRenderTargetDescription
    {
        VkExtent2D extent{};
        std::vector<VulkanRenderTargetColorAttachmentDescription> colorAttachments;
        std::optional<VulkanRenderTargetDepthAttachmentDescription> depthAttachment;
    };
}