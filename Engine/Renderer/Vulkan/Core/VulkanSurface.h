#pragma once

#include <vulkan/vulkan.h>

namespace Kosmos
{
    class Window;
    class VulkanInstance;

    class VulkanSurface
    {
        public:
            VulkanSurface(VulkanInstance& instance, Window& window);
            ~VulkanSurface();

            VulkanSurface(const VulkanSurface&) = delete;
            VulkanSurface& operator=(const VulkanSurface&) = delete;

            VkSurfaceKHR GetHandle() const { return m_Surface; }

        private:
            VulkanInstance& m_Instance;
            VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
    };
}