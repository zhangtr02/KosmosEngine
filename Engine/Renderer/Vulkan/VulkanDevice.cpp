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

    bool VulkanDevice::TryFindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags requiredProperties, uint32_t& memoryTypeIndex) const
    {
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memoryProperties);

        for (uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index)
        {
            const bool isSupported = (typeFilter & (1u << index)) != 0;
            const VkMemoryPropertyFlags availableProperties = memoryProperties.memoryTypes[index].propertyFlags;
            const bool hasRequiredProperties = (availableProperties & requiredProperties) == requiredProperties;

            if (isSupported && hasRequiredProperties)
            {
                memoryTypeIndex = index;
                return true;
            }
        }

        return false;
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
        const VkCommandBuffer commandBuffer = BeginSingleTimeCommands(commandPool);

        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = sourceOffset;
        copyRegion.dstOffset = destinationOffset;
        copyRegion.size = size;

        vkCmdCopyBuffer(commandBuffer, source.GetHandle(), destination.GetHandle(), 1, &copyRegion);
        EndSingleTimeCommands(commandBuffer, commandPool);
    }

    void VulkanDevice::CopyBufferToImage(const VulkanBuffer& source, VkImage destination, uint32_t width, uint32_t height)
    {
        if (destination == VK_NULL_HANDLE)
        {
            throw std::runtime_error("Cannot copy into a null Vulkan image!");
        }

        if (width == 0 || height == 0)
        {
            throw std::runtime_error("Cannot copy into a zero-sized Vulkan image!");
        }

        if ((source.GetUsage() & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) == 0)
        {
            throw std::runtime_error("Source buffer does not support transfer source usage!");
        }

        VkCommandPool commandPool = VK_NULL_HANDLE;
        const VkCommandBuffer commandBuffer = BeginSingleTimeCommands(commandPool);

        VkBufferImageCopy copyRegion{};
        copyRegion.bufferOffset = 0;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageOffset = {0, 0, 0};
        copyRegion.imageExtent = {width, height, 1};

        vkCmdCopyBufferToImage(
            commandBuffer,
            source.GetHandle(),
            destination,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &copyRegion);

        EndSingleTimeCommands(commandBuffer, commandPool);
    }

    void VulkanDevice::TransitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout)
    {
        if (image == VK_NULL_HANDLE)
        {
            throw std::runtime_error("Cannot transition a null Vulkan image!");
        }

        VkAccessFlags sourceAccessMask = 0;
        VkAccessFlags destinationAccessMask = 0;
        VkPipelineStageFlags sourceStage = 0;
        VkPipelineStageFlags destinationStage = 0;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
            newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            sourceAccessMask = 0;
            destinationAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
                newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            sourceAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            destinationAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else
        {
            throw std::runtime_error("Unsupported Vulkan image layout transition!");
        }

        VkCommandPool commandPool = VK_NULL_HANDLE;
        const VkCommandBuffer commandBuffer = BeginSingleTimeCommands(commandPool);

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = sourceAccessMask;
        barrier.dstAccessMask = destinationAccessMask;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(
            commandBuffer,
            sourceStage,
            destinationStage,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier);

        EndSingleTimeCommands(commandBuffer, commandPool);
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

    VkCommandBuffer VulkanDevice::BeginSingleTimeCommands(VkCommandPool& commandPool)
    {
        commandPool = VK_NULL_HANDLE;

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = m_QueueFamilyIndices.graphicsFamily.value();

        if (vkCreateCommandPool(m_Device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create single-time command pool!");
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
            commandPool = VK_NULL_HANDLE;
            throw std::runtime_error("Failed to allocate single-time command buffer!");
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
        {
            vkDestroyCommandPool(m_Device, commandPool, nullptr);
            commandPool = VK_NULL_HANDLE;
            throw std::runtime_error("Failed to begin single-time command buffer!");
        }

        return commandBuffer;
    }

    void VulkanDevice::EndSingleTimeCommands(VkCommandBuffer commandBuffer, VkCommandPool commandPool)
    {
        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
        {
            vkDestroyCommandPool(m_Device, commandPool, nullptr);
            throw std::runtime_error("Failed to end single-time command buffer!");
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        if (vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
        {
            vkDestroyCommandPool(m_Device, commandPool, nullptr);
            throw std::runtime_error("Failed to submit single-time command buffer!");
        }

        const VkResult waitResult = vkQueueWaitIdle(m_GraphicsQueue);
        vkDestroyCommandPool(m_Device, commandPool, nullptr);

        if (waitResult != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to wait for single-time command buffer!");
        }
    }
}