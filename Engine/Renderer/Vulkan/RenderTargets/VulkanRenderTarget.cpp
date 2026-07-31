#include "Renderer/Vulkan/RenderTargets/VulkanRenderTarget.h"
#include "Renderer/Vulkan/RenderTargets/VulkanRenderTargetDescription.h"
#include "Renderer/Vulkan/Core/VulkanDevice.h"
#include "Renderer/Vulkan/Resources/VulkanImage.h"

#include <array>
#include <stdexcept>
#include <vector>

namespace Kosmos
{
    VulkanRenderTarget::VulkanRenderTarget(VulkanDevice& device, const VulkanRenderTargetDescription& description)
        : m_Device(device), m_Extent(description.extent)
    {
        if (m_Extent.width == 0 || m_Extent.height == 0)
        {
            throw std::runtime_error("Cannot create a render target with zero extent!");
        }

        if (description.colorAttachments.empty() && !description.depthAttachment)
        {
            throw std::runtime_error("Render target requires at least one attachment!");
        }

        CreateImages(description);
        CreateRenderPass(description);
        CreateFramebuffer();
    }

    VulkanRenderTarget::~VulkanRenderTarget()
    {
        if (m_Framebuffer != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(m_Device.GetHandle(), m_Framebuffer, nullptr);
        }

        if (m_RenderPass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(m_Device.GetHandle(), m_RenderPass, nullptr);
        }
    }

    bool VulkanRenderTarget::SupportsFormat(VkFormat format, VkFormatFeatureFlags requiredFeatures) const
    {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(m_Device.GetPhysicalDevice(), format, &properties);
        return (properties.optimalTilingFeatures & requiredFeatures) == requiredFeatures;
    }

    VkFormat VulkanRenderTarget::FindDepthFormat(VkFormatFeatureFlags requiredFeatures) const
    {
        constexpr std::array<VkFormat, 3> candidates = {
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D24_UNORM_S8_UINT
        };

        for (VkFormat format : candidates)
        {
            if (SupportsFormat(format, requiredFeatures))
            {
                return format;
            }
        }

        throw std::runtime_error("Failed to find a supported render-target depth format!");
    }

    void VulkanRenderTarget::CreateImages(const VulkanRenderTargetDescription& description)
    {
        m_ColorImages.reserve(description.colorAttachments.size());

        for (const VulkanRenderTargetColorAttachmentDescription& attachment : description.colorAttachments)
        {
            if (attachment.format == VK_FORMAT_UNDEFINED)
            {
                throw std::runtime_error("Render-target color format cannot be undefined!");
            }

            VkFormatFeatureFlags requiredFeatures = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;

            if ((attachment.usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0)
            {
                requiredFeatures |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
            }

            if ((attachment.usage & VK_IMAGE_USAGE_STORAGE_BIT) != 0)
            {
                requiredFeatures |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
            }

            if (!SupportsFormat(attachment.format, requiredFeatures))
            {
                throw std::runtime_error("Requested render-target color format does not support the required features!");
            }

            m_ColorImages.push_back(std::make_unique<VulkanImage>(
                m_Device,
                m_Extent.width,
                m_Extent.height,
                attachment.format,
                attachment.usage | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT));
        }

        if (description.depthAttachment)
        {
            const VulkanRenderTargetDepthAttachmentDescription& attachment = *description.depthAttachment;
            VkFormatFeatureFlags requiredFeatures = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;

            if ((attachment.usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0)
            {
                requiredFeatures |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
            }

            const VkFormat format = attachment.format == VK_FORMAT_UNDEFINED ? FindDepthFormat(requiredFeatures) : attachment.format;

            if (!SupportsFormat(format, requiredFeatures))
            {
                throw std::runtime_error("Requested render-target depth format does not support the required features!");
            }

            m_DepthImage = std::make_unique<VulkanImage>(
                m_Device,
                m_Extent.width,
                m_Extent.height,
                format,
                attachment.usage | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT);
        }
    }

    void VulkanRenderTarget::CreateRenderPass(const VulkanRenderTargetDescription& description)
    {
        std::vector<VkAttachmentDescription> attachments;
        std::vector<VkAttachmentReference> colorReferences;
        attachments.reserve(description.colorAttachments.size() + (description.depthAttachment ? 1 : 0));
        colorReferences.reserve(description.colorAttachments.size());

        for (size_t index = 0; index < description.colorAttachments.size(); ++index)
        {
            const VulkanRenderTargetColorAttachmentDescription& source = description.colorAttachments[index];

            VkAttachmentDescription attachment{};
            attachment.format = source.format;
            attachment.samples = VK_SAMPLE_COUNT_1_BIT;
            attachment.loadOp = source.loadOp;
            attachment.storeOp = source.storeOp;
            attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout = source.initialLayout;
            attachment.finalLayout = source.finalLayout;
            attachments.push_back(attachment);

            VkAttachmentReference reference{};
            reference.attachment = static_cast<uint32_t>(index);
            reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colorReferences.push_back(reference);
        }

        VkAttachmentReference depthReference{};

        if (description.depthAttachment)
        {
            const VulkanRenderTargetDepthAttachmentDescription& source = *description.depthAttachment;

            VkAttachmentDescription attachment{};
            attachment.format = m_DepthImage->GetFormat();
            attachment.samples = VK_SAMPLE_COUNT_1_BIT;
            attachment.loadOp = source.loadOp;
            attachment.storeOp = source.storeOp;
            attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout = source.initialLayout;
            attachment.finalLayout = source.finalLayout;
            attachments.push_back(attachment);

            depthReference.attachment = static_cast<uint32_t>(attachments.size() - 1);
            depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
        subpass.pColorAttachments = colorReferences.empty() ? nullptr : colorReferences.data();
        subpass.pDepthStencilAttachment = description.depthAttachment ? &depthReference : nullptr;

        VkPipelineStageFlags attachmentStages = 0;
        VkAccessFlags attachmentAccess = 0;

        if (!description.colorAttachments.empty())
        {
            attachmentStages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            attachmentAccess |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        }

        if (description.depthAttachment)
        {
            attachmentStages |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            attachmentAccess |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }

        std::array<VkSubpassDependency, 2> dependencies{};

        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        dependencies[0].dstStageMask = attachmentStages;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[0].dstAccessMask = attachmentAccess;
        dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = attachmentStages;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        dependencies[1].srcAccessMask = attachmentAccess;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        createInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        createInfo.pAttachments = attachments.data();
        createInfo.subpassCount = 1;
        createInfo.pSubpasses = &subpass;
        createInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        createInfo.pDependencies = dependencies.data();

        if (vkCreateRenderPass(m_Device.GetHandle(), &createInfo, nullptr, &m_RenderPass) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create render-target render pass!");
        }
    }

    void VulkanRenderTarget::CreateFramebuffer()
    {
        std::vector<VkImageView> attachments;
        attachments.reserve(m_ColorImages.size() + (m_DepthImage ? 1 : 0));

        for (const std::unique_ptr<VulkanImage>& colorImage : m_ColorImages)
        {
            attachments.push_back(colorImage->GetImageView());
        }

        if (m_DepthImage)
        {
            attachments.push_back(m_DepthImage->GetImageView());
        }

        VkFramebufferCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        createInfo.renderPass = m_RenderPass;
        createInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        createInfo.pAttachments = attachments.data();
        createInfo.width = m_Extent.width;
        createInfo.height = m_Extent.height;
        createInfo.layers = 1;

        if (vkCreateFramebuffer(m_Device.GetHandle(), &createInfo, nullptr, &m_Framebuffer) != VK_SUCCESS)
        {
            vkDestroyRenderPass(m_Device.GetHandle(), m_RenderPass, nullptr);
            m_RenderPass = VK_NULL_HANDLE;
            throw std::runtime_error("Failed to create render-target framebuffer!");
        }
    }
}