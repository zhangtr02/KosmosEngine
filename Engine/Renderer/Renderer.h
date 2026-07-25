#pragma once

#include <memory>

namespace Kosmos
{
    class Window;
    class VulkanContext;
    class Camera;
    class Scene;

    class Renderer
    {
        public:
            Renderer(Window& window, const Camera& camera, const Scene& scene);
            ~Renderer();

            Renderer(const Renderer&) = delete;
            Renderer& operator=(const Renderer&) = delete;

            void DrawFrame();
            void WaitIdle();

        private:
            std::unique_ptr<VulkanContext> m_Context;
    };
}