#include "Core/Application.h"
#include "Core/Window.h"
#include "Core/Input.h"
#include "Core/ApplicationLayer.h"
#include "Renderer/Renderer.h"
#include "Scene/Camera/Camera.h"
#include "Scene/Camera/CameraController.h"
#include "Scene/Scene.h"

#include <chrono>
#include <stdexcept>
#include <utility>

namespace
{
    void UpdateRenderDebugView(const Kosmos::Input& input, Kosmos::Renderer& renderer)
    {
        if (input.IsKeyDown(Kosmos::Key::Digit0))
        {
            renderer.GetSettings().debugView = Kosmos::RenderDebugView::FinalColor;
        }
        else if (input.IsKeyDown(Kosmos::Key::Digit1))
        {
            renderer.GetSettings().debugView = Kosmos::RenderDebugView::Albedo;
        }
        else if (input.IsKeyDown(Kosmos::Key::Digit2))
        {
            renderer.GetSettings().debugView = Kosmos::RenderDebugView::WorldNormal;
        }
        else if (input.IsKeyDown(Kosmos::Key::Digit3))
        {
            renderer.GetSettings().debugView = Kosmos::RenderDebugView::Roughness;
        }
        else if (input.IsKeyDown(Kosmos::Key::Digit4))
        {
            renderer.GetSettings().debugView = Kosmos::RenderDebugView::Metallic;
        }
        else if (input.IsKeyDown(Kosmos::Key::Digit5))
        {
            renderer.GetSettings().debugView = Kosmos::RenderDebugView::Depth;
        }
        else if (input.IsKeyDown(Kosmos::Key::Digit6))
        {
            renderer.GetSettings().debugView = Kosmos::RenderDebugView::RawAmbientOcclusion;
        }
        else if (input.IsKeyDown(Kosmos::Key::Digit7))
        {
            renderer.GetSettings().debugView = Kosmos::RenderDebugView::AmbientOcclusion;
        }
        else if (input.IsKeyDown(Kosmos::Key::Digit8))
        {
            renderer.GetSettings().debugView = Kosmos::RenderDebugView::SceneColor;
        }
        else if (input.IsKeyDown(Kosmos::Key::Digit9))
        {
            renderer.GetSettings().debugView = Kosmos::RenderDebugView::Bloom;
        }
    }
}

namespace Kosmos
{
    Application::Application(std::unique_ptr<Scene> scene, std::unique_ptr<Camera> camera, std::unique_ptr<ApplicationLayer> applicationLayer)
    {
        if (!scene)
        {
            throw std::runtime_error("Application scene cannot be null!");
        }

        if (!camera)
        {
            throw std::runtime_error("Application camera cannot be null!");
        }

        m_Window = std::make_unique<Window>(1280, 720, "Kosmos Engine");
        m_Input = std::make_unique<Input>(*m_Window);
        m_Camera = std::move(camera);
        m_CameraController = std::make_unique<CameraController>(*m_Input, *m_Camera);
        m_Scene = std::move(scene);
        m_Renderer = std::make_unique<Renderer>(*m_Window, *m_Camera, *m_Scene);
        m_ApplicationLayer = std::move(applicationLayer);

        if (m_ApplicationLayer)
        {
            m_Renderer->EnableGui();
            m_ApplicationLayer->OnAttach(*m_Renderer, *m_Scene);
        }
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

            const bool capturesMouse = m_Renderer->WantsGuiMouseInput();
            const bool capturesKeyboard = m_Renderer->WantsGuiKeyboardInput();

            if (!capturesMouse && !capturesKeyboard)
            {
                m_CameraController->Update(deltaTime);
            }

            if (!capturesKeyboard)
            {
                UpdateRenderDebugView(*m_Input, *m_Renderer);
            }

            if (m_ApplicationLayer)
            {
                m_ApplicationLayer->OnUpdate(deltaTime);
                m_Renderer->BeginGuiFrame();
                m_ApplicationLayer->OnGui();
                m_Renderer->EndGuiFrame();
            }

            m_Renderer->DrawFrame(deltaTime);
        }

        m_Renderer->WaitIdle();
    }
}
