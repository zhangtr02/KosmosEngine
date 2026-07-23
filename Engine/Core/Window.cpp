#include "Core/Window.h"
#include <stdexcept>

namespace Kosmos
{
    Window::Window(int width, int height, const std::string& title)
    {
        if (!glfwInit())
        {
            throw std::runtime_error("Failed to initialize GLFW");
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

        m_Window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
        if (!m_Window)
        {
            glfwTerminate();
            throw std::runtime_error("Failed to create GLFW window");
        }

        glfwSetWindowUserPointer(m_Window, this);
        glfwSetFramebufferSizeCallback(m_Window, FramebufferResizeCallback);
    }

    Window::~Window()
    {
        if (m_Window != nullptr)
        {
            glfwDestroyWindow(m_Window);
            m_Window = nullptr;
        }

        glfwTerminate();
    }

    bool Window::ShouldClose() const
    {
        return glfwWindowShouldClose(m_Window);
    }

    void Window::PollEvents() const
    {
        glfwPollEvents();
    }

    void Window::WaitEvents() const
    {
        glfwWaitEvents();
    }

    void Window::GetFramebufferSize(int& width, int& height) const
    {
        glfwGetFramebufferSize(m_Window, &width, &height);
    }

    void Window::FramebufferResizeCallback(GLFWwindow* window, int width, int height)
    {
        Window* self = static_cast<Window*>(glfwGetWindowUserPointer(window));

        if (self == nullptr)
        {
            return;
        }
        
        self->m_FramebufferResized = true;
    }
}