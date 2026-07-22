#pragma once

#include <memory>

namespace Kosmos
{
    class Window;
    class Renderer;

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
            std::unique_ptr<Renderer> m_Renderer;
            
    };
}