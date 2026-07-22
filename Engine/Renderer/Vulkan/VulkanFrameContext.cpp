#include "Renderer/Vulkan/VulkanFrameContext.h"
#include "Renderer/Vulkan/VulkanDevice.h"

#include <stdexcept>

namespace Kosmos
{
    VulkanFrameContext::VulkanFrameContext(VulkanDevice& device)
        : m_Device(device)
    {
        CreateCommandResources();
        CreateSynchronizationObjects();
    }

    VulkanFrameContext::~VulkanFrameContext()
    {
        const VkDevice device = m_Device.GetHandle();

        if (m_InFlightFence != VK_NULL_HANDLE)
        {
            vkDestroyFence(device, m_InFlightFence, nullptr);
        }

        if (m_ImageAvailableSemaphore != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device, m_ImageAvailableSemaphore, nullptr);
        }

        if (m_CommandPool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(device, m_CommandPool, nullptr);
        }
    }

    void VulkanFrameContext::CreateCommandResources()
    {
        const QueueFamilyIndices& indices = m_Device.GetQueueFamilyIndices();

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = indices.graphicsFamily.value();

        if (vkCreateCommandPool(m_Device.GetHandle(), &poolInfo, nullptr, &m_CommandPool) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create command pool!");
        }

        VkCommandBufferAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.commandPool = m_CommandPool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(m_Device.GetHandle(), &allocateInfo, &m_CommandBuffer) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate command buffer!");
        }
    }

    void VulkanFrameContext::CreateSynchronizationObjects()
    {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        if (vkCreateSemaphore(m_Device.GetHandle(), &semaphoreInfo, nullptr, &m_ImageAvailableSemaphore) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create image-available semaphore!");
        }

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        if (vkCreateFence(m_Device.GetHandle(), &fenceInfo, nullptr, &m_InFlightFence) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create in-flight fence!");
        }
    }

    void VulkanFrameContext::WaitForFence() const
    {
        if (vkWaitForFences(m_Device.GetHandle(), 1, &m_InFlightFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to wait for frame fence!");
        }
    }

    void VulkanFrameContext::ResetFence() const
    {
        if (vkResetFences(m_Device.GetHandle(), 1, &m_InFlightFence) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to reset frame fence!");
        }
    }

    void VulkanFrameContext::ResetCommandBuffer() const
    {
        if (vkResetCommandBuffer(m_CommandBuffer, 0) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to reset command buffer!");
        }
    }
}