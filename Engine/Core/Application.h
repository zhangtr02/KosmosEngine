#pragma once

#include <memory>

namespace Kosmos
{
    class Window;
    class Renderer;
    class Input;
    class Camera;
    class CameraController;

    class Application
    {
        public:
            Application();
            ~Application();

            Application(const Application&) = delete;
            Application& operator=(const Application&) = delete;

            void Run();
        
        private:
            std::unique_ptr<Window> m_Window;
            std::unique_ptr<Input> m_Input;
            std::unique_ptr<Camera> m_Camera;
            std::unique_ptr<CameraController> m_CameraController;
            std::unique_ptr<Renderer> m_Renderer;
    };
}