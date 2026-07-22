#pragma once

#include <vulkan/vulkan.h>
#include <vector>

namespace Kosmos
{
    class VulkanInstance
    {
        public:
            VulkanInstance();
            ~VulkanInstance();

            VulkanInstance(const VulkanInstance&) = delete;
            VulkanInstance& operator=(const VulkanInstance&) = delete;

            VkInstance GetHandle() const { return m_Instance; }

        private:
            static bool CheckValidationLayerSupport();
            static bool CheckInstanceExtensionSupport(const std::vector<const char*>& requiredExtensions);
            static std::vector<const char*> GetRequiredExtensions();
            static void PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
            void CreateDebugMessenger();
            static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                              VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                              const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
                                                              void* userData);

        private:
            VkInstance m_Instance = VK_NULL_HANDLE;
            VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
    };
}