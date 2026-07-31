#pragma once

#include <memory>

namespace Kosmos
{
    class Window;
    class Renderer;
    class Input;
    class Camera;
    class CameraController;
    class Scene;
    class ApplicationLayer;

    class Application
    {
        public:
            Application(std::unique_ptr<Scene> scene, std::unique_ptr<Camera> camera, std::unique_ptr<ApplicationLayer> applicationLayer = nullptr);
            ~Application();

            Application(const Application&) = delete;
            Application& operator=(const Application&) = delete;

            void Run();

        private:
            std::unique_ptr<Window> m_Window;
            std::unique_ptr<Input> m_Input;
            std::unique_ptr<Camera> m_Camera;
            std::unique_ptr<CameraController> m_CameraController;
            std::unique_ptr<Scene> m_Scene;
            std::unique_ptr<Renderer> m_Renderer;
            std::unique_ptr<ApplicationLayer> m_ApplicationLayer;
    };
}