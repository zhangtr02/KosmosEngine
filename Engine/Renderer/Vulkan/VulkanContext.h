#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <array>

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
            static constexpr uint32_t MaxFramesInFlight = 2;

            void RecreateSwapchain();
            void RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);

        private:
            Window& m_Window;

            std::unique_ptr<VulkanInstance> m_Instance;
            std::unique_ptr<VulkanSurface> m_Surface;
            std::unique_ptr<VulkanDevice> m_Device;
            std::unique_ptr<VulkanSwapchain> m_Swapchain;
            std::unique_ptr<VulkanPipeline> m_Pipeline;
            
            std::array<std::unique_ptr<VulkanFrameContext>, MaxFramesInFlight> m_FrameContexts;
            uint32_t m_CurrentFrameIndex = 0;
    };
}