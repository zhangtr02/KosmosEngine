#include "Renderer/Vulkan/VulkanSwapchain.h"
#include "Core/Window.h"
#include "Renderer/Vulkan/VulkanDevice.h"
#include "Renderer/Vulkan/VulkanSurface.h"

#include <algorithm>
#include <stdexcept>

namespace
{
    struct SwapchainSupportDetails
    {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    SwapchainSupportDetails QuerySwapchainSupport(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
    {
        SwapchainSupportDetails details;

        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &details.capabilities);

        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
        details.formats.resize(formatCount);

        if (formatCount > 0)
        {
            vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, details.formats.data());
        }

        uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
        details.presentModes.resize(presentModeCount);
        
        if (presentModeCount > 0)
        {
            vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, details.presentModes.data());
        }

        return details;
    }

    VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats)
    {
        for (const VkSurfaceFormatKHR& format : formats)
        {
            if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
                format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                return format;
            }
        }

        return formats[0];
    }

    VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& presentModes)
    {
        for (const VkPresentModeKHR& presentMode : presentModes)
        {
            if (presentMode == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                return presentMode;
            }
        }

        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, const Kosmos::Window& window)
    {
        if (capabilities.currentExtent.width != UINT32_MAX)
        {
            return capabilities.currentExtent;
        }

        int framebufferWidth = 0;
        int framebufferHeight = 0;

        window.GetFramebufferSize(framebufferWidth, framebufferHeight);

        VkExtent2D extent = {
            static_cast<uint32_t>(framebufferWidth),
            static_cast<uint32_t>(framebufferHeight)
        };

        extent.width = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

        return extent;
    }
}


namespace Kosmos
{
    VulkanSwapchain::VulkanSwapchain(Window& window, VulkanDevice& device, VulkanSurface& surface, VkSwapchainKHR oldSwapchain)
        : m_Window(window), m_Device(device), m_Surface(surface)
    {
        CreateSwapchain(oldSwapchain);
        CreateImageViews();
        CreateRenderPass();
        CreateFramebuffers();
        CreateRenderFinishedSemaphores();
    }

    VulkanSwapchain::~VulkanSwapchain()
    {
        const VkDevice device = m_Device.GetHandle();

        for (VkSemaphore semaphore : m_RenderFinishedSemaphores)
        {
            if (semaphore != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(device, semaphore, nullptr);
            }
        }

        for (VkFramebuffer framebuffer : m_Framebuffers)
        {
            if (framebuffer != VK_NULL_HANDLE)
            {
                vkDestroyFramebuffer(device, framebuffer, nullptr);
            }
        }

        if (m_RenderPass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(device, m_RenderPass, nullptr);
        }

        for (VkImageView imageView : m_ImageViews)
        {
            if (imageView != VK_NULL_HANDLE)
            {
                vkDestroyImageView(device, imageView, nullptr);
            }
        }

        if (m_Swapchain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(device, m_Swapchain, nullptr);
        }
    }

    void VulkanSwapchain::CreateSwapchain(VkSwapchainKHR oldSwapchain)
    {
        const SwapchainSupportDetails swapchainSupport = QuerySwapchainSupport(m_Device.GetPhysicalDevice(), m_Surface.GetHandle());

        const VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapchainSupport.formats);
        const VkPresentModeKHR presentMode = ChooseSwapPresentMode(swapchainSupport.presentModes);
        const VkExtent2D extent = ChooseExtent(swapchainSupport.capabilities, m_Window);

        uint32_t imageCount = swapchainSupport.capabilities.minImageCount + 1;
        if (swapchainSupport.capabilities.maxImageCount > 0 && imageCount > swapchainSupport.capabilities.maxImageCount)
        {
            imageCount = swapchainSupport.capabilities.maxImageCount;
        }

        const QueueFamilyIndices& indices = m_Device.GetQueueFamilyIndices();
        const uint32_t queueFamilies[] = {
            indices.graphicsFamily.value(),
            indices.presentFamily.value()
        };

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = m_Surface.GetHandle();
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        if (indices.graphicsFamily != indices.presentFamily)
        {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilies;
        }
        else
        {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        createInfo.preTransform = swapchainSupport.capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = oldSwapchain;

        if (vkCreateSwapchainKHR(m_Device.GetHandle(), &createInfo, nullptr, &m_Swapchain) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create swap chain!");
        }

        vkGetSwapchainImagesKHR(m_Device.GetHandle(), m_Swapchain, &imageCount, nullptr);
        m_Images.resize(imageCount);
        vkGetSwapchainImagesKHR(m_Device.GetHandle(), m_Swapchain, &imageCount, m_Images.data());

        m_ImageFormat = surfaceFormat.format;
        m_Extent = extent;
    }

    void VulkanSwapchain::CreateImageViews()
    {
        m_ImageViews.reserve(m_Images.size());

        for (VkImage image : m_Images)
        {
            VkImageViewCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image = image;
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = m_ImageFormat;
            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel = 0;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount = 1;

            VkImageView imageView = VK_NULL_HANDLE;

            if (vkCreateImageView(m_Device.GetHandle(), &createInfo, nullptr, &imageView) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create image view!");
            }

            m_ImageViews.push_back(imageView);
        }
    }

    void VulkanSwapchain::CreateRenderPass()
    {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = m_ImageFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorReference{};
        colorReference.attachment = 0;
        colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorReference;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        createInfo.attachmentCount = 1;
        createInfo.pAttachments = &colorAttachment;
        createInfo.subpassCount = 1;
        createInfo.pSubpasses = &subpass;
        createInfo.dependencyCount = 1;
        createInfo.pDependencies = &dependency;

        if (vkCreateRenderPass(m_Device.GetHandle(), &createInfo, nullptr, &m_RenderPass) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create render pass!");
        }
    }

    void VulkanSwapchain::CreateFramebuffers()
    {
        m_Framebuffers.reserve(m_ImageViews.size());

        for (VkImageView imageView : m_ImageViews)
        {
            const VkImageView attachments[] = {
                imageView
            };

            VkFramebufferCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            createInfo.renderPass = m_RenderPass;
            createInfo.attachmentCount = 1;
            createInfo.pAttachments = attachments;
            createInfo.width = m_Extent.width;
            createInfo.height = m_Extent.height;
            createInfo.layers = 1;

            VkFramebuffer framebuffer = VK_NULL_HANDLE;

            if (vkCreateFramebuffer(m_Device.GetHandle(), &createInfo, nullptr, &framebuffer) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create framebuffer!");
            }

            m_Framebuffers.push_back(framebuffer);
        }
    }

    void VulkanSwapchain::CreateRenderFinishedSemaphores()
    {
        m_RenderFinishedSemaphores.resize(m_Images.size());

        VkSemaphoreCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        for (VkSemaphore& semaphore : m_RenderFinishedSemaphores)
        {
            if (vkCreateSemaphore(m_Device.GetHandle(), &createInfo, nullptr, &semaphore) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create render-finished semaphore!");
            }
        }
    }

    VkResult VulkanSwapchain::AcquireNextImage(VkSemaphore imageAvailableSemaphore, uint32_t& imageIndex) const
    {
        return vkAcquireNextImageKHR(
            m_Device.GetHandle(),
            m_Swapchain,
            UINT64_MAX,
            imageAvailableSemaphore,
            VK_NULL_HANDLE,
            &imageIndex);
    }

    VkResult VulkanSwapchain::Present(uint32_t imageIndex) const
    {
        const VkSemaphore waitSemaphores[] = {
            m_RenderFinishedSemaphores[imageIndex]
        };

        const VkSwapchainKHR swapchains[] = {
            m_Swapchain
        };

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = waitSemaphores;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapchains;
        presentInfo.pImageIndices = &imageIndex;

        return vkQueuePresentKHR(m_Device.GetPresentQueue(), &presentInfo);
    }
}