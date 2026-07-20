#pragma once

#include <vulkan/vulkan.h>
#include <vector>

namespace Kosmos
{
    class Window;

    class VulkanContext
    {
        public:
            explicit VulkanContext(Window& window);
            ~VulkanContext();

            VulkanContext(const VulkanContext&) = delete;
            VulkanContext& operator=(const VulkanContext&) = delete;

            void DrawFrame();
            void WaitIdle();

        private:
            void CreateInstance();
            void CreateSurface();
            void PickPhysicalDevice();
            void CreateLogicalDevice();
            void CreateSwapchain();
            void CreateImageViews();
            void CreateRenderPass();
            void CreateGraphicsPipeline();
            void CreateFramebuffers();
            void CreateCommandPool();
            void CreateCommandBuffer();
            void CreateSyncObjects();

            void RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);


        private:
            Window& m_Window;

            VkInstance m_Instance = VK_NULL_HANDLE;
            VkSurfaceKHR m_Surface = VK_NULL_HANDLE;

            VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
            VkDevice m_Device = VK_NULL_HANDLE;

            VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
            VkQueue m_PresentQueue = VK_NULL_HANDLE;

            VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
            VkFormat m_SwapchainImageFormat = VK_FORMAT_UNDEFINED;
            VkExtent2D m_SwapchainExtent = {};

            std::vector<VkImage> m_SwapchainImages;
            std::vector<VkImageView> m_SwapchainImageViews;
            std::vector<VkFramebuffer> m_SwapchainFramebuffers;

            VkRenderPass m_RenderPass = VK_NULL_HANDLE;
            VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
            VkPipeline m_GraphicsPipeline = VK_NULL_HANDLE;

            VkCommandPool m_CommandPool = VK_NULL_HANDLE;
            VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;

            VkSemaphore m_ImageAvailableSemaphore = VK_NULL_HANDLE;
            VkSemaphore m_RenderFinishedSemaphore = VK_NULL_HANDLE;
            VkFence m_InFlightFence = VK_NULL_HANDLE;
    };
}