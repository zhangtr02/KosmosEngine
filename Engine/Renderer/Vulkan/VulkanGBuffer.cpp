#include "Renderer/Vulkan/VulkanGBuffer.h"
#include "Renderer/Vulkan/VulkanRenderTarget.h"
#include "Renderer/Vulkan/VulkanRenderTargetDescription.h"
#include "Renderer/Vulkan/VulkanImage.h"

#include <array>

namespace
{
    constexpr VkFormat AlbedoAmbientOcclusionFormat = VK_FORMAT_R8G8B8A8_UNORM;
    constexpr VkFormat NormalRoughnessFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    constexpr VkFormat MaterialParametersFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

    Kosmos::VulkanRenderTargetColorAttachmentDescription CreateColorAttachment(VkFormat format)
    {
        Kosmos::VulkanRenderTargetColorAttachmentDescription attachment{};
        attachment.format = format;
        attachment.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        return attachment;
    }
}

namespace Kosmos
{
    VulkanGBuffer::VulkanGBuffer(VulkanDevice& device, VkExtent2D extent)
    {
        VulkanRenderTargetDescription description{};
        description.extent = extent;
        description.colorAttachments.reserve(ColorAttachmentCount);
        description.colorAttachments.push_back(CreateColorAttachment(AlbedoAmbientOcclusionFormat));
        description.colorAttachments.push_back(CreateColorAttachment(NormalRoughnessFormat));
        description.colorAttachments.push_back(CreateColorAttachment(MaterialParametersFormat));

        VulkanRenderTargetDepthAttachmentDescription depthAttachment{};
        depthAttachment.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        description.depthAttachment = depthAttachment;

        m_RenderTarget = std::make_unique<VulkanRenderTarget>(device, description);
    }

    VulkanGBuffer::~VulkanGBuffer() = default;

    VkRenderPass VulkanGBuffer::GetRenderPass() const
    {
        return m_RenderTarget->GetRenderPass();
    }

    VkFramebuffer VulkanGBuffer::GetFramebuffer() const
    {
        return m_RenderTarget->GetFramebuffer();
    }

    VkExtent2D VulkanGBuffer::GetExtent() const
    {
        return m_RenderTarget->GetExtent();
    }

    VkImageView VulkanGBuffer::GetAlbedoAmbientOcclusionImageView() const
    {
        return m_RenderTarget->GetColorImage(0).GetImageView();
    }

    VkImageView VulkanGBuffer::GetNormalRoughnessImageView() const
    {
        return m_RenderTarget->GetColorImage(1).GetImageView();
    }

    VkImageView VulkanGBuffer::GetMaterialParametersImageView() const
    {
        return m_RenderTarget->GetColorImage(2).GetImageView();
    }

    VkImageView VulkanGBuffer::GetDepthImageView() const
    {
        return m_RenderTarget->GetDepthImage()->GetImageView();
    }
}