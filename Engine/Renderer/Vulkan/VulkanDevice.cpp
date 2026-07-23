#include "Renderer/Vulkan/VulkanDevice.h"
#include "Renderer/Vulkan/VulkanInstance.h"
#include "Renderer/Vulkan/VulkanSurface.h"
#include "Renderer/Vulkan/VulkanBuffer.h"

#include <array>
#include <vector>
#include <set>
#include <string>
#include <stdexcept>

namespace
{
    constexpr std::array<const char*, 1> DeviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
}

namespace Kosmos
{
    VulkanDevice::VulkanDevice(VulkanInstance& instance, VulkanSurface& surface)
        : m_Instance(instance), m_Surface(surface)
    {
        PickPhysicalDevice();
        CreateLogicalDevice();
    }

    VulkanDevice::~VulkanDevice()
    {
        if (m_Device != VK_NULL_HANDLE)
        {
            vkDestroyDevice(m_Device, nullptr);
        }
    }

    QueueFamilyIndices VulkanDevice::FindQueueFamilies(VkPhysicalDevice device) const
    {
        QueueFamilyIndices indices;

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        for (uint32_t i = 0; i < queueFamilyCount; ++i)
        {
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                indices.graphicsFamily = i;
            }

            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_Surface.GetHandle(), &presentSupport);

            if (presentSupport == VK_TRUE)
            {
                indices.presentFamily = i;
            }

            if (indices.IsComplete())
            {
                break;
            }
        }

        return indices;
    }

    bool VulkanDevice::CheckDeviceExtensionSupport(VkPhysicalDevice device) const
    {
        uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

        std::set<std::string> requiredExtensions(DeviceExtensions.begin(), DeviceExtensions.end());

        for (const VkExtensionProperties& extension : availableExtensions)
        {
            requiredExtensions.erase(extension.extensionName);
        }

        return requiredExtensions.empty();
    }

    bool VulkanDevice::HasSwapChainSupport(VkPhysicalDevice device) const
    {
        uint32_t formatCount = 0;
        uint32_t presentModeCount = 0;

        vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_Surface.GetHandle(), &formatCount, nullptr);
        
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_Surface.GetHandle(), &presentModeCount, nullptr);

        return formatCount > 0 && presentModeCount > 0;
    }

    bool VulkanDevice::IsDeviceSuitable(VkPhysicalDevice device) const
    {
        const QueueFamilyIndices indices = FindQueueFamilies(device);

        return indices.IsComplete() && CheckDeviceExtensionSupport(device) && HasSwapChainSupport(device);
    }

    void VulkanDevice::PickPhysicalDevice()
    {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(m_Instance.GetHandle(), &deviceCount, nullptr);

        if (deviceCount == 0)
        {
            throw std::runtime_error("Failed to find GPUs with Vulkan support!");
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(m_Instance.GetHandle(), &deviceCount, devices.data());

        for (VkPhysicalDevice device : devices)
        {
            if (IsDeviceSuitable(device))
            {
                m_PhysicalDevice = device;
                m_QueueFamilyIndices = FindQueueFamilies(device);
                break;
            }
        }

        if (m_PhysicalDevice == VK_NULL_HANDLE)
        {
            throw std::runtime_error("Failed to find a suitable GPU!");
        }
    }

    void VulkanDevice::CreateLogicalDevice()
    {
        const std::set<uint32_t> uniqueQueueFamilies = {
            m_QueueFamilyIndices.graphicsFamily.value(),
            m_QueueFamilyIndices.presentFamily.value()
        };

        const float queuePriority = 1.0f;

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        queueCreateInfos.reserve(uniqueQueueFamilies.size());

        for (uint32_t queueFamily : uniqueQueueFamilies)
        {
            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = queueFamily;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(queueInfo);
        }

        VkPhysicalDeviceFeatures deviceFeatures{};

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(DeviceExtensions.size());
        createInfo.ppEnabledExtensionNames = DeviceExtensions.data();

        if (vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_Device) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create logical device!");
        }

        vkGetDeviceQueue(m_Device, m_QueueFamilyIndices.graphicsFamily.value(), 0, &m_GraphicsQueue);
        vkGetDeviceQueue(m_Device, m_QueueFamilyIndices.presentFamily.value(), 0, &m_PresentQueue);
    }

    uint32_t VulkanDevice::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags requiredProperties) const
    {
        VkPhysicalDeviceMemoryProperties memoryProperties{};

        vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memoryProperties);

        for (uint32_t memoryTypeIndex = 0; memoryTypeIndex < memoryProperties.memoryTypeCount; ++memoryTypeIndex)
        {
            const bool isSupported = (typeFilter & (1u << memoryTypeIndex)) != 0;
            const VkMemoryPropertyFlags availableProperties = memoryProperties.memoryTypes[memoryTypeIndex].propertyFlags;
            const bool hasRequiredProperties = (availableProperties & requiredProperties) == requiredProperties;

            if (isSupported && hasRequiredProperties)
            {
                return memoryTypeIndex;
            }
        }

        throw std::runtime_error("Failed to find a suitable Vulkan memory type!");
    }

        
    void VulkanDevice::CopyBuffer(const VulkanBuffer& source, VulkanBuffer& destination, VkDeviceSize size, VkDeviceSize sourceOffset, VkDeviceSize destinationOffset)
    {
        if (size == 0)
        {
            throw std::runtime_error("Cannot copy zero bytes between Vulkan buffers!");
        }

        if ((source.GetUsage() & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) == 0)
        {
            throw std::runtime_error("Source buffer does not support transfer source usage!");
        }

        if ((destination.GetUsage() & VK_BUFFER_USAGE_TRANSFER_DST_BIT) == 0)
        {
            throw std::runtime_error("Destination buffer does not support transfer destination usage!");
        }

        if (sourceOffset > source.GetSize() || size > source.GetSize() - sourceOffset)
        {
            throw std::runtime_error("Vulkan source buffer copy is out of bounds!");
        }

        if (destinationOffset > destination.GetSize() || size > destination.GetSize() - destinationOffset)
        {
            throw std::runtime_error("Vulkan destination buffer copy is out of bounds!");
        }

        VkCommandPool commandPool = VK_NULL_HANDLE;

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = m_QueueFamilyIndices.graphicsFamily.value();

        if (vkCreateCommandPool(m_Device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create upload command pool!");
        }

        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

        VkCommandBufferAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.commandPool = commandPool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(m_Device, &allocateInfo, &commandBuffer) != VK_SUCCESS)
        {
            vkDestroyCommandPool(m_Device, commandPool, nullptr);

            throw std::runtime_error("Failed to allocate upload command buffer!");
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
        {
            vkDestroyCommandPool(m_Device, commandPool, nullptr);

            throw std::runtime_error("Failed to begin upload command buffer!");
        }

        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = sourceOffset;
        copyRegion.dstOffset = destinationOffset;
        copyRegion.size = size;

        vkCmdCopyBuffer(commandBuffer, source.GetHandle(), destination.GetHandle(), 1, &copyRegion);

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
        {
            vkDestroyCommandPool(m_Device, commandPool, nullptr);

            throw std::runtime_error("Failed to end upload command buffer!");
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        if (vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
        {
            vkDestroyCommandPool(m_Device, commandPool, nullptr);

            throw std::runtime_error("Failed to submit buffer copy command!");
        }

        if (vkQueueWaitIdle(m_GraphicsQueue) != VK_SUCCESS)
        {
            vkDestroyCommandPool(m_Device, commandPool, nullptr);

            throw std::runtime_error("Failed to wait for buffer copy command!");
        }

        vkDestroyCommandPool(m_Device, commandPool, nullptr);
    }

    void VulkanDevice::UploadBuffer(const void* data, VkDeviceSize size, VulkanBuffer& destination, VkDeviceSize destinationOffset)
    {
        if (data == nullptr)
        {
            throw std::runtime_error("Cannot upload null data to Vulkan buffer!");
        }

        if (size == 0)
        {
            throw std::runtime_error("Cannot upload zero bytes to Vulkan buffer!");
        }

        if (destinationOffset > destination.GetSize() || size > destination.GetSize() - destinationOffset)
        {
            throw std::runtime_error("Vulkan destination buffer upload is out of bounds!");
        }

        VulkanBuffer stagingBuffer(*this, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        stagingBuffer.Write(data, size);

        CopyBuffer(stagingBuffer, destination, size, 0, destinationOffset);
    }

    void VulkanDevice::WaitIdle() const
    {
        if (m_Device == VK_NULL_HANDLE)
        {
            return;
        }

        if (vkDeviceWaitIdle(m_Device) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to wait for Vulkan device!");
        }
    }
}