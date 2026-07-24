#include "Renderer/Vulkan/VulkanContext.h"
#include "Core/Window.h"
#include "Renderer/Vulkan/VulkanInstance.h"
#include "Renderer/Vulkan/VulkanSurface.h"
#include "Renderer/Vulkan/VulkanDevice.h"
#include "Renderer/Vulkan/VulkanSwapchain.h"
#include "Renderer/Vulkan/VulkanPipeline.h"
#include "Renderer/Vulkan/VulkanFrameContext.h"
#include "Renderer/Vertex.h"
#include "Renderer/Vulkan/VulkanBuffer.h"

#include <memory>
#include <stdexcept>
#include <utility>
#include <array>

namespace Kosmos
{
    namespace
    {
        const std::array<Vertex, 4> Vertices = {
            Vertex{{-0.5f, -0.5f}, {1.0f, 0.1f, 0.1f}},
            Vertex{{ 0.5f, -0.5f}, {0.1f, 1.0f, 0.1f}},
            Vertex{{ 0.5f,  0.5f}, {0.1f, 0.1f, 1.0f}},
            Vertex{{-0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}}
        };

        const std::array<uint16_t, 6> Indices = {
            0, 1, 2,
            2, 3, 0
        };
    }

    VulkanContext::VulkanContext(Window& window)
        : m_Window(window)
    {
        m_Instance = std::make_unique<VulkanInstance>();
        m_Surface = std::make_unique<VulkanSurface>(*m_Instance, m_Window);
        m_Device = std::make_unique<VulkanDevice>(*m_Instance, *m_Surface);

        CreateGeometryBuffers();

        m_Swapchain = std::make_unique<VulkanSwapchain>(m_Window, *m_Device, *m_Surface);
        m_Pipeline = std::make_unique<VulkanPipeline>(*m_Device, m_Swapchain->GetRenderPass(), m_Swapchain->GetExtent());
        
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

     void VulkanContext::CreateGeometryBuffers()
    {
        const VkDeviceSize vertexBufferSize = sizeof(Vertex) * Vertices.size();

        m_VertexBuffer = std::make_unique<VulkanBuffer>(
            *m_Device,
            vertexBufferSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        m_Device->UploadBuffer(Vertices.data(), vertexBufferSize, *m_VertexBuffer);

        const VkDeviceSize indexBufferSize = sizeof(uint16_t) * Indices.size();

        m_IndexBuffer = std::make_unique<VulkanBuffer>(
            *m_Device,
            indexBufferSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        m_Device->UploadBuffer(Indices.data(), indexBufferSize, *m_IndexBuffer);

        m_IndexCount = static_cast<uint32_t>(Indices.size());
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
        auto newPipeline = std::make_unique<VulkanPipeline>(*m_Device, newSwapchain->GetRenderPass(), newSwapchain->GetExtent());

        m_Device->WaitIdle();

        m_Pipeline = std::move(newPipeline);
        m_Swapchain = std::move(newSwapchain);
    }

    void VulkanContext::RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to begin command buffer!");
        }

        const VkClearValue clearColor = {
            {{0.02f, 0.03f, 0.04f, 1.0f}}
        };

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = m_Swapchain->GetRenderPass();
        renderPassInfo.framebuffer = m_Swapchain->GetFramebuffer(imageIndex);
        renderPassInfo.renderArea.offset = { 0, 0 };
        renderPassInfo.renderArea.extent = m_Swapchain->GetExtent();
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearColor;

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline->GetHandle());

        const VkBuffer vertexBuffers[] = {
            m_VertexBuffer->GetHandle()
        };

        const VkDeviceSize vertexOffsets[] = {
            0
        };

        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, vertexOffsets);
        vkCmdBindIndexBuffer(commandBuffer, m_IndexBuffer->GetHandle(), 0, VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(commandBuffer, m_IndexCount, 1, 0, 0, 0);

        vkCmdEndRenderPass(commandBuffer);

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to record command buffer!");
        }
    }

    void VulkanContext::DrawFrame()
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
        RecordCommandBuffer(commandBuffer, imageIndex);

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