#pragma once

#include <vulkan/vulkan.h>
#include <memory>

namespace Kosmos
{
    class Window;
    class VulkanInstance;
    class VulkanSurface;
    class VulkanDevice;
    class VulkanSwapchain;
    class VulkanPipeline;
    class VulkanFrameContext;

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
            void RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);

        private:
            Window& m_Window;

            std::unique_ptr<VulkanInstance> m_Instance;
            std::unique_ptr<VulkanSurface> m_Surface;
            std::unique_ptr<VulkanDevice> m_Device;
            std::unique_ptr<VulkanSwapchain> m_Swapchain;
            std::unique_ptr<VulkanPipeline> m_Pipeline;
            std::unique_ptr<VulkanFrameContext> m_FrameContext;
    };
}