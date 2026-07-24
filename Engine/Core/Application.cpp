#include "Core/Application.h"
#include "Core/Window.h"
#include "Core/Input.h"
#include "Scene/Camera.h"
#include "Scene/CameraController.h"
#include "Renderer/Renderer.h"

#include <glm/glm.hpp>
#include <chrono>

namespace Kosmos
{
    Application::Application()
    {
        m_Window = std::make_unique<Window>(1280, 720, "Kosmos Engine");
        m_Input = std::make_unique<Input>(*m_Window);

        m_Camera = std::make_unique<Camera>(
            glm::vec3(3.6f, 2.7f, 5.0f),
            glm::radians(-126.0f),
            glm::radians(-22.0f));

        m_CameraController = std::make_unique<CameraController>(*m_Input, *m_Camera);
        m_Renderer = std::make_unique<Renderer>(*m_Window, *m_Camera);
    }

    Application::~Application() = default;

    void Application::Run()
    {
        auto previousTime = std::chrono::steady_clock::now();

        while (!m_Window->ShouldClose())
        {
            m_Window->PollEvents();

            const auto currentTime = std::chrono::steady_clock::now();
            const float deltaTime = std::chrono::duration<float>(currentTime - previousTime).count();
            previousTime = currentTime;

            m_CameraController->Update(deltaTime);
            m_Renderer->DrawFrame();
        }

        m_Renderer->WaitIdle();
    }
}