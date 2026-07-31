#include "Core/Application.h"
#include "Core/Window.h"
#include "Core/Input.h"
#include "Renderer/Renderer.h"
#include "Scene/Camera/Camera.h"
#include "Scene/Camera/CameraController.h"
#include "Scene/Scene.h"
#include "Scene/DemoScene.h"

#include <glm/glm.hpp>
#include <chrono>

namespace
{
    void UpdateRenderDebugView(const Kosmos::Input& input, Kosmos::Renderer& renderer)
    {
        if (input.IsKeyDown(Kosmos::Key::Digit0)) renderer.GetSettings().debugView = Kosmos::RenderDebugView::FinalColor;
        else if (input.IsKeyDown(Kosmos::Key::Digit1)) renderer.GetSettings().debugView = Kosmos::RenderDebugView::Albedo;
        else if (input.IsKeyDown(Kosmos::Key::Digit2)) renderer.GetSettings().debugView = Kosmos::RenderDebugView::WorldNormal;
        else if (input.IsKeyDown(Kosmos::Key::Digit3)) renderer.GetSettings().debugView = Kosmos::RenderDebugView::Roughness;
        else if (input.IsKeyDown(Kosmos::Key::Digit4)) renderer.GetSettings().debugView = Kosmos::RenderDebugView::Metallic;
        else if (input.IsKeyDown(Kosmos::Key::Digit5)) renderer.GetSettings().debugView = Kosmos::RenderDebugView::Depth;
        else if (input.IsKeyDown(Kosmos::Key::Digit6)) renderer.GetSettings().debugView = Kosmos::RenderDebugView::RawAmbientOcclusion;
        else if (input.IsKeyDown(Kosmos::Key::Digit7)) renderer.GetSettings().debugView = Kosmos::RenderDebugView::AmbientOcclusion;
        else if (input.IsKeyDown(Kosmos::Key::Digit8)) renderer.GetSettings().debugView = Kosmos::RenderDebugView::SceneColor;
        else if (input.IsKeyDown(Kosmos::Key::Digit9)) renderer.GetSettings().debugView = Kosmos::RenderDebugView::Bloom;
    }
}

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
        m_Scene = CreateDemoScene();
        m_Renderer = std::make_unique<Renderer>(*m_Window, *m_Camera, *m_Scene);
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
            UpdateRenderDebugView(*m_Input, *m_Renderer);
            m_Renderer->DrawFrame(deltaTime);
        }

        m_Renderer->WaitIdle();
    }
}
