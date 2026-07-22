#include "Renderer/Vulkan/VulkanSurface.h"
#include "Core/Window.h"
#include "Renderer/Vulkan/VulkanInstance.h"

#include <GLFW/glfw3.h>
#include <stdexcept>

namespace Kosmos
{
    VulkanSurface::VulkanSurface(VulkanInstance& instance, Window& window)
        : m_Instance(instance)
    {
        if (glfwCreateWindowSurface(m_Instance.GetHandle(), window.GetNativeWindow(), nullptr, &m_Surface) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create Vulkan surface!");
        }
    }

    VulkanSurface::~VulkanSurface()
    {
        if (m_Surface != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(m_Instance.GetHandle(), m_Surface, nullptr);
        }
    }
}