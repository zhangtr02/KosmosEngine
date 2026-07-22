#pragma once

#include <vulkan/vulkan.h>

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
            VkInstance m_Instance = VK_NULL_HANDLE;
    };
}