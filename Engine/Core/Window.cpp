#include "Core/Window.h"
#include <stdexcept>

namespace Kosmos
{
    Window::Window(int width, int height, const std::string& title)
        : m_Width(width), m_Height(height)
    {
        if (!glfwInit())
        {
            throw std::runtime_error("Failed to initialize GLFW");
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

        m_Window = glfwCreateWindow(m_Width, m_Height, title.c_str(), nullptr, nullptr);
        if (!m_Window)
        {
            glfwTerminate();
            throw std::runtime_error("Failed to create GLFW window");
        }
    }

    Window::~Window()
    {
        glfwDestroyWindow(m_Window);
        glfwTerminate();
    }
}