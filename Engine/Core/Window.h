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
            void WaitEvents() const;

            void GetFramebufferSize(int& width, int& height) const;

            bool WasFramebufferResized() const { return m_FramebufferResized; }
            void ResetFramebufferResized() { m_FramebufferResized = false; }
            GLFWwindow* GetNativeWindow() const { return m_Window; }
        
        private:
            static void FramebufferResizeCallback(GLFWwindow* window, int width, int height);
        
        private:
            GLFWwindow* m_Window = nullptr;
            bool m_FramebufferResized = false;
    };
}