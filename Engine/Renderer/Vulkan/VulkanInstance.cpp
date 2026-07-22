#include "Renderer/Vulkan/VulkanInstance.h"

#include <GLFW/glfw3.h>
#include <stdexcept>
#include <array>
#include <iostream>

namespace
{
#ifdef NDEBUG
    constexpr bool EnableValidationLayers = false;
#else
    constexpr bool EnableValidationLayers = true;
#endif

    constexpr std::array<const char*, 1> ValidationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    const char* GetSeverityName(VkDebugUtilsMessageSeverityFlagBitsEXT severity)
    {
        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        {
            return "Error";
        }
        
        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        {
            return "Warning";
        }

        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        {
            return "Info";
        }

        return "Verbose";
    }

    const char* GetMessageTypeName(VkDebugUtilsMessageTypeFlagsEXT messageType)
    {
        if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
        {
            return "Validation";
        }

        if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
        {
            return "Performance";
        }

        return "General";
    }
}

namespace Kosmos
{
    VulkanInstance::VulkanInstance()
    {
        if (EnableValidationLayers && !CheckValidationLayerSupport())
        {
            throw std::runtime_error("VK_LAYER_KHRONOS_validation is not available!");
        }

        VkApplicationInfo applicationInfo{};
        applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        applicationInfo.pApplicationName = "Kosmos Sandbox";
        applicationInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        applicationInfo.pEngineName = "Kosmos Engine";
        applicationInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        applicationInfo.apiVersion = VK_API_VERSION_1_4;

        const std::vector<const char*> requiredExtensions = GetRequiredExtensions();

        if (!CheckInstanceExtensionSupport(requiredExtensions))
        {
            throw std::runtime_error("One or more required Vulkan instance extensions are not available!");
        }

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &applicationInfo;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size());
        createInfo.ppEnabledExtensionNames = requiredExtensions.data();

        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};

        if (EnableValidationLayers)
        {
            createInfo.enabledLayerCount = static_cast<uint32_t>(ValidationLayers.size());
            createInfo.ppEnabledLayerNames = ValidationLayers.data();

            PopulateDebugMessengerCreateInfo(debugCreateInfo);
            createInfo.pNext = &debugCreateInfo;
        }
        else
        {
            createInfo.enabledLayerCount = 0;
            createInfo.ppEnabledLayerNames = nullptr;
            createInfo.pNext = nullptr;
        }

        if (vkCreateInstance(&createInfo, nullptr, &m_Instance) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create Vulkan instance!");
        }

        if (EnableValidationLayers)
        {
            CreateDebugMessenger();
        }
    }

    VulkanInstance::~VulkanInstance()
    {
        if (m_DebugMessenger != VK_NULL_HANDLE)
        {
            const auto destroyDebugMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugUtilsMessengerEXT"));

            if (destroyDebugMessenger)
            {
                destroyDebugMessenger(m_Instance, m_DebugMessenger, nullptr);
            }
        }

        if (m_Instance != VK_NULL_HANDLE)
        {
            vkDestroyInstance(m_Instance, nullptr);
        }
    }

    bool VulkanInstance::CheckValidationLayerSupport()
    {
        uint32_t availableLayerCount = 0;

        if (vkEnumerateInstanceLayerProperties(&availableLayerCount, nullptr) != VK_SUCCESS)
        {
            return false;
        }

        std::vector<VkLayerProperties> availableLayers(availableLayerCount);

        if (vkEnumerateInstanceLayerProperties(&availableLayerCount, availableLayers.data()) != VK_SUCCESS)
        {
            return false;
        }

        for (const char* requiredLayer : ValidationLayers)
        {
            bool layerFound = false;

            for (const VkLayerProperties& availableLayer : availableLayers)
            {
                if (strcmp(requiredLayer, availableLayer.layerName) == 0)
                {
                    layerFound = true;
                    break;
                }
            }

            if (!layerFound)
            {
                return false;
            }
        }

        return true;
    }

    bool VulkanInstance::CheckInstanceExtensionSupport(const std::vector<const char*>& requiredExtensions)
    {
        uint32_t availableExtensionCount = 0;

        if (vkEnumerateInstanceExtensionProperties(nullptr, &availableExtensionCount, nullptr) != VK_SUCCESS)
        {
            return false;
        }

        std::vector<VkExtensionProperties> availableExtensions(availableExtensionCount);

        if (vkEnumerateInstanceExtensionProperties(nullptr, &availableExtensionCount, availableExtensions.data()) != VK_SUCCESS)
        {
            return false;
        }

        for (const char* requiredExtension : requiredExtensions)
        {
            bool extensionFound = false;

            for (const VkExtensionProperties& availableExtension : availableExtensions)
            {
                if (strcmp(requiredExtension, availableExtension.extensionName) == 0)
                {
                    extensionFound = true;
                    break;
                }
            }

            if (!extensionFound)
            {
                return false;
            }
        }

        return true;
    }

    std::vector<const char*> VulkanInstance::GetRequiredExtensions()
    {
        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        if (glfwExtensions == nullptr || glfwExtensionCount == 0)
        {
            throw std::runtime_error("Failed to get required Vulkan extensions from GLFW!");
        }

        std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

        if (EnableValidationLayers)
        {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        return extensions;
    }

    void VulkanInstance::PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo)
    {
        createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity = 
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    
        createInfo.messageType = 
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;

        createInfo.pfnUserCallback = DebugCallback;
        createInfo.pUserData = nullptr;
    }

    void VulkanInstance::CreateDebugMessenger()
    {
        const auto createDebugUtilsMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(m_Instance, "vkCreateDebugUtilsMessengerEXT"));

        if (createDebugUtilsMessenger == nullptr)
        {
            throw std::runtime_error("vkCreateDebugUtilsMessengerEXT is unavailable!");
        }

        VkDebugUtilsMessengerCreateInfoEXT createInfo{};
        PopulateDebugMessengerCreateInfo(createInfo);

        if (createDebugUtilsMessenger(m_Instance, &createInfo, nullptr, &m_DebugMessenger) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create Vulkan debug messenger!");
        }
    }

    VKAPI_ATTR VkBool32 VKAPI_CALL VulkanInstance::DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                                  VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                                  const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
                                                                  void* userData)
    {
        (void)userData;

        std::cerr
            << "[Vulkan "
            << GetMessageTypeName(messageType)
            << " "
            << GetSeverityName(messageSeverity)
            << "]";

        if (callbackData->pMessageIdName)
        {
            std::cerr
                << " ["
                << callbackData->pMessageIdName
                << "]";
        }

        std::cerr
            << '\n'
            << callbackData->pMessage
            << "\n\n";

        return VK_FALSE;
    }
}