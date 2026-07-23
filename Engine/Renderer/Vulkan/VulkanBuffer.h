#pragma once

#include <vulkan/vulkan.h>

namespace Kosmos
{
    class VulkanDevice;

    class VulkanBuffer
    {
        public:
            VulkanBuffer(VulkanDevice& device, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryProperties);
            ~VulkanBuffer();

            VulkanBuffer(const VulkanBuffer&) = delete;
            VulkanBuffer& operator=(const VulkanBuffer&) = delete;

            void Write(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);

            VkBuffer GetHandle() const { return m_Buffer; }
            VkDeviceSize GetSize() const { return m_Size; }
            VkBufferUsageFlags GetUsage() const { return m_Usage; }
            VkMemoryPropertyFlags GetMemoryProperties() const { return m_MemoryProperties; }

        private:
            VulkanDevice& m_Device;

            VkBuffer m_Buffer = VK_NULL_HANDLE;
            VkDeviceMemory m_Memory = VK_NULL_HANDLE;

            VkDeviceSize m_Size = 0;
            VkBufferUsageFlags m_Usage = 0;
            VkMemoryPropertyFlags m_MemoryProperties = 0;
    };
}