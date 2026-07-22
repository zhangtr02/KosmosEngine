#pragma once

#include <memory>

namespace Kosmos
{
    class Window;
    class VulkanContext;

    class Renderer
    {
        public:
            explicit Renderer(Window& window);
            ~Renderer();

            Renderer(const Renderer&) = delete;
            Renderer& operator=(const Renderer&) = delete;

            void DrawFrame();
            void WaitIdle();

        private:
            std::unique_ptr<VulkanContext> m_Context;
    };
}