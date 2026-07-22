#pragma once

#include <vulkan/vulkan.h>

namespace Kosmos
{
    class VulkanDevice;

    class VulkanFrameContext
    {
        public:
            explicit VulkanFrameContext(VulkanDevice& device);
            ~VulkanFrameContext();

            VulkanFrameContext(const VulkanFrameContext&) = delete;
            VulkanFrameContext& operator=(const VulkanFrameContext&) = delete;

            void WaitForFence() const;
            void ResetFence() const;
            void ResetCommandBuffer() const;

            VkCommandBuffer GetCommandBuffer() const { return m_CommandBuffer; }
            VkSemaphore GetImageAvailableSemaphore() const { return m_ImageAvailableSemaphore; }
            VkFence GetInFlightFence() const { return m_InFlightFence; }

        private:
            void CreateCommandResources();
            void CreateSynchronizationObjects();

        private:
            VulkanDevice& m_Device;

            VkCommandPool m_CommandPool = VK_NULL_HANDLE;
            VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;

            VkSemaphore m_ImageAvailableSemaphore = VK_NULL_HANDLE;
            VkFence m_InFlightFence = VK_NULL_HANDLE;
    };
}