#include "Renderer/Vulkan/VulkanContext.h"
#include "Core/Window.h"
#include "Renderer/Vulkan/VulkanInstance.h"
#include "Renderer/Vulkan/VulkanSurface.h"
#include "Renderer/Vulkan/VulkanDevice.h"
#include "Renderer/Vulkan/VulkanSwapchain.h"
#include "Renderer/Vulkan/VulkanFrameContext.h"
#include "Renderer/Vulkan/VulkanSceneRenderer.h"

#include <memory>
#include <stdexcept>

namespace Kosmos
{
    VulkanContext::VulkanContext(Window& window, const Camera& camera, const Scene& scene, const RenderSettings& settings)
        : m_Window(window)
    {
        m_Instance = std::make_unique<VulkanInstance>();
        m_Surface = std::make_unique<VulkanSurface>(*m_Instance, m_Window);
        m_Device = std::make_unique<VulkanDevice>(*m_Instance, *m_Surface);
        m_Swapchain = std::make_unique<VulkanSwapchain>(m_Window, *m_Device, *m_Surface);
        m_SceneRenderer = std::make_unique<VulkanSceneRenderer>(*m_Device, camera, scene, settings, m_Swapchain->GetRenderPass(), m_Swapchain->GetExtent(), MaxFramesInFlight);

        for (std::unique_ptr<VulkanFrameContext>& frameContext : m_FrameContexts)
        {
            frameContext = std::make_unique<VulkanFrameContext>(*m_Device);
        }
    }

    VulkanContext::~VulkanContext()
    {
        if (m_Device && m_Device->GetHandle() != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(m_Device->GetHandle());
        }
    }

    void VulkanContext::RecreateSwapchain()
    {
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        m_Window.GetFramebufferSize(framebufferWidth, framebufferHeight);

        while (framebufferWidth == 0 || framebufferHeight == 0)
        {
            if (m_Window.ShouldClose())
            {
                return;
            }

            m_Window.WaitEvents();
            m_Window.GetFramebufferSize(framebufferWidth, framebufferHeight);
        }

        if (m_Window.ShouldClose())
        {
            return;
        }

        const VkSwapchainKHR oldSwapchain = m_Swapchain->GetHandle();
        auto newSwapchain = std::make_unique<VulkanSwapchain>(m_Window, *m_Device, *m_Surface, oldSwapchain);
        m_SceneRenderer->RecreateView(newSwapchain->GetRenderPass(), newSwapchain->GetExtent());
        m_Swapchain = std::move(newSwapchain);
    }

    void VulkanContext::DrawFrame(float deltaTime)
    {
        VulkanFrameContext& frame = *m_FrameContexts[m_CurrentFrameIndex];
        frame.WaitForFence();

        uint32_t imageIndex = 0;
        const VkResult acquireResult = m_Swapchain->AcquireNextImage(frame.GetImageAvailableSemaphore(), imageIndex);

        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
        {
            m_Window.ResetFramebufferResized();
            RecreateSwapchain();
            return;
        }

        if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
        {
            throw std::runtime_error("Failed to acquire swapchain image!");
        }

        frame.ResetCommandBuffer();

        const VkCommandBuffer commandBuffer = frame.GetCommandBuffer();
        m_SceneRenderer->RecordFrame(commandBuffer, m_Swapchain->GetFramebuffer(imageIndex), m_CurrentFrameIndex, deltaTime);

        frame.ResetFence();

        const VkSemaphore imageAvailableSemaphores[] = {
            frame.GetImageAvailableSemaphore()
        };

        const VkSemaphore renderFinishedSemaphores[] = {
            m_Swapchain->GetRenderFinishedSemaphore(imageIndex)
        };

        const VkPipelineStageFlags waitStages[] = {
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
        };

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = imageAvailableSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = renderFinishedSemaphores;

        if (vkQueueSubmit(m_Device->GetGraphicsQueue(), 1, &submitInfo, frame.GetInFlightFence()) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to submit command buffer!");
        }

        const VkResult presentResult = m_Swapchain->Present(imageIndex);

        if (presentResult != VK_SUCCESS && presentResult != VK_SUBOPTIMAL_KHR && presentResult != VK_ERROR_OUT_OF_DATE_KHR)
        {
            throw std::runtime_error("Failed to present swapchain image!");
        }

        const bool shouldRecreate =
            acquireResult == VK_SUBOPTIMAL_KHR ||
            presentResult == VK_SUBOPTIMAL_KHR ||
            presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
            m_Window.WasFramebufferResized();

        if (shouldRecreate)
        {
            m_Window.ResetFramebufferResized();
            RecreateSwapchain();
        }

        m_CurrentFrameIndex = (m_CurrentFrameIndex + 1) % MaxFramesInFlight;
    }

    void VulkanContext::WaitIdle()
    {
        if (m_Device)
        {
            m_Device->WaitIdle();
        }
    }
}
