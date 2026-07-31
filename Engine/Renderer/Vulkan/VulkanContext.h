#pragma once

#include <vulkan/vulkan.h>
#include <array>
#include <memory>

namespace Kosmos
{
    class Window;
    class Camera;
    class Scene;
    struct RenderSettings;
    class VulkanInstance;
    class VulkanSurface;
    class VulkanDevice;
    class VulkanSwapchain;
    class VulkanFrameContext;
    class VulkanSceneRenderer;
    class VulkanPresentPass;

    class VulkanContext
    {
        public:
            VulkanContext(Window& window, const Camera& camera, const Scene& scene, const RenderSettings& settings);
            ~VulkanContext();

            VulkanContext(const VulkanContext&) = delete;
            VulkanContext& operator=(const VulkanContext&) = delete;

            void DrawFrame(float deltaTime);
            void WaitIdle();

        private:
            static constexpr uint32_t MaxFramesInFlight = 2;

            void RecreateSwapchain();
            void RecordCommandBuffer(VkCommandBuffer commandBuffer, VkFramebuffer swapchainFramebuffer, uint32_t frameIndex, float deltaTime);

        private:
            Window& m_Window;
            const RenderSettings& m_Settings;
            std::unique_ptr<VulkanInstance> m_Instance;
            std::unique_ptr<VulkanSurface> m_Surface;
            std::unique_ptr<VulkanDevice> m_Device;
            std::unique_ptr<VulkanSwapchain> m_Swapchain;
            std::unique_ptr<VulkanSceneRenderer> m_SceneRenderer;
            std::unique_ptr<VulkanPresentPass> m_PresentPass;
            std::array<std::unique_ptr<VulkanFrameContext>, MaxFramesInFlight> m_FrameContexts;
            uint32_t m_CurrentFrameIndex = 0;
    };
}
