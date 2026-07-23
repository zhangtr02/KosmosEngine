#include "Renderer/Vulkan/VulkanBuffer.h"
#include "Renderer/Vulkan/VulkanDevice.h"

#include <cstring>
#include <stdexcept>

namespace Kosmos
{
    VulkanBuffer::VulkanBuffer(VulkanDevice& device, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryProperties)
        : m_Device(device), m_Size(size), m_Usage(usage), m_MemoryProperties(memoryProperties)
    {
        if (size == 0)
        {
            throw std::runtime_error("Cannot create a Vulkan buffer with size zero!");
        }

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(m_Device.GetHandle(), &bufferInfo, nullptr, &m_Buffer) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create Vulkan buffer!");
        }

        VkMemoryRequirements memoryRequirements{};
        vkGetBufferMemoryRequirements(m_Device.GetHandle(), m_Buffer, &memoryRequirements);

        VkMemoryAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = memoryRequirements.size;
        allocateInfo.memoryTypeIndex = m_Device.FindMemoryType(memoryRequirements.memoryTypeBits, memoryProperties);

        if (vkAllocateMemory(m_Device.GetHandle(), &allocateInfo, nullptr, &m_Memory) != VK_SUCCESS)
        {
            vkDestroyBuffer(m_Device.GetHandle(), m_Buffer, nullptr);
            m_Buffer = VK_NULL_HANDLE;
            throw std::runtime_error("Failed to allocate Vulkan buffer memory!");
        }

        if (vkBindBufferMemory(m_Device.GetHandle(), m_Buffer, m_Memory, 0) != VK_SUCCESS)
        {
            vkFreeMemory(m_Device.GetHandle(), m_Memory, nullptr);
            vkDestroyBuffer(m_Device.GetHandle(), m_Buffer, nullptr);
            m_Memory = VK_NULL_HANDLE;
            m_Buffer = VK_NULL_HANDLE;
            throw std::runtime_error("Failed to bind Vulkan buffer memory!");
        }
    }

    VulkanBuffer::~VulkanBuffer()
    {
        if (m_Buffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(m_Device.GetHandle(), m_Buffer, nullptr);
        }

        if (m_Memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(m_Device.GetHandle(), m_Memory, nullptr);
        }
    }

    void VulkanBuffer::Write(const void* data, VkDeviceSize size, VkDeviceSize offset)
    {
        if (data == nullptr)
        {
            throw std::runtime_error("Cannot write null data to Vulkan buffer!");
        }

        if (size == 0)
        {
            throw std::runtime_error("Cannot write zero bytes to Vulkan buffer!");
        }

        if (offset > m_Size || size > m_Size - offset)
        {
            throw std::runtime_error("Vulkan buffer write is out of bounds!");
        }

        if ((m_MemoryProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0)
        {
            throw std::runtime_error("Cannot map memory that is not host visible!");
        }

        void* mappedData = nullptr;

        if (vkMapMemory(m_Device.GetHandle(), m_Memory, 0, VK_WHOLE_SIZE, 0, &mappedData) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to map Vulkan buffer memory!");
        }

        std::memcpy(static_cast<char*>(mappedData) + offset, data, static_cast<size_t>(size));

        if ((m_MemoryProperties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
        {
            VkMappedMemoryRange memoryRange{};
            memoryRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            memoryRange.memory = m_Memory;
            memoryRange.offset = 0;
            memoryRange.size = VK_WHOLE_SIZE;

            if (vkFlushMappedMemoryRanges(m_Device.GetHandle(), 1, &memoryRange) != VK_SUCCESS)
            {
                vkUnmapMemory(m_Device.GetHandle(), m_Memory);
                throw std::runtime_error("Failed to flush Vulkan buffer memory!");
            }
        }

        vkUnmapMemory(m_Device.GetHandle(), m_Memory);
    }
}