#include "Renderer/Vulkan/VulkanDevice.h"
#include "Renderer/Vulkan/VulkanInstance.h"
#include "Renderer/Vulkan/VulkanSurface.h"

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