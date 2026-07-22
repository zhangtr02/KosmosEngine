#include "Core/Application.h"
#include "Core/Window.h"
#include "Renderer/Renderer.h"

namespace Kosmos
{
    Application::Application()
    {
        m_Window = std::make_unique<Window>(1280, 720, "Kosmos Engine");
        m_Renderer = std::make_unique<Renderer>(*m_Window);
    }

    Application::~Application() = default;

    void Application::Run()
    {
        while (!m_Window->ShouldClose())
        {
            m_Window->PollEvents();
            m_Renderer->DrawFrame();
        }
        m_Renderer->WaitIdle();
    }
}