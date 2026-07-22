#pragma once

#include <GLFW/glfw3.h>
#include <string>

namespace Kosmos
{
    class Window
    {
        public:
            Window(int width, int height, const std::string& title);
            ~Window();

            Window(const Window&) = delete;
            Window& operator=(const Window&) = delete;
            
            bool ShouldClose() const;
            void PollEvents() const;

            int GetWidth() const { return m_Width; }
            int GetHeight() const { return m_Height; }
            GLFWwindow* GetNativeWindow() const { return m_Window; }
        
        private:
            GLFWwindow* m_Window = nullptr;
            int m_Width = 0;
            int m_Height = 0;
    };
}