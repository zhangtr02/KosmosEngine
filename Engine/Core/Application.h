#pragma once

#include "Core/Window.h"
#include "Renderer/Renderer.h"

namespace Kosmos
{
    class Application
    {
        public:
            Application();
            ~Application();

            void Run();
        
        private:
            Window m_Window;
            Renderer m_Renderer;
            
    };
}