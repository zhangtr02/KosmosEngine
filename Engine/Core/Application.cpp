#include "Core/Application.h"

namespace Kosmos
{
    Application::Application()
        : m_Window(1280, 720, "Kosmos Engine"), m_Renderer(m_Window)
    {
    }

    Application::~Application()
    {
    }

    void Application::Run()
    {
        while (!m_Window.ShouldClose())
        {
            m_Window.PollEvents();
            m_Renderer.DrawFrame();
        }
        m_Renderer.WaitIdle();
    }
}