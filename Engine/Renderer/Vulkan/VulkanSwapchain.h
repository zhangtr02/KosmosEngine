#pragma once

#include <vulkan/vulkan.h>
#include <vector>

namespace Kosmos
{
    class Window;
    class VulkanDevice;
    class VulkanSurface;

    class VulkanSwapchain
    {
        public:
            VulkanSwapchain(Window& window, VulkanDevice& device, VulkanSurface& surface);
            ~VulkanSwapchain();

            VulkanSwapchain(const VulkanSwapchain&) = delete;
            VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

            VkResult AcquireNextImage(VkSemaphore imageAvailableSemaphore, uint32_t& imageIndex) const;

            VkResult Present(VkSemaphore renderFinishedSemaphore, uint32_t imageIndex) const;

            VkRenderPass GetRenderPass() const { return m_RenderPass; }
            VkExtent2D GetExtent() const { return m_Extent; }
            VkFormat GetImageFormat() const { return m_ImageFormat; }

            VkFramebuffer GetFramebuffer(uint32_t index) const { return m_Framebuffers[index]; }

            size_t GetImageCount() const { return m_Images.size(); }

        private:
            void CreateSwapchain();
            void CreateImageViews();
            void CreateRenderPass();
            void CreateFramebuffers();

        private:
            Window& m_Window;
            VulkanDevice& m_Device;
            VulkanSurface& m_Surface;

            VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
            VkFormat m_ImageFormat = VK_FORMAT_UNDEFINED;
            VkExtent2D m_Extent = {};

            std::vector<VkImage> m_Images;
            std::vector<VkImageView> m_ImageViews;

            VkRenderPass m_RenderPass = VK_NULL_HANDLE;
            std::vector<VkFramebuffer> m_Framebuffers;            
    };
}