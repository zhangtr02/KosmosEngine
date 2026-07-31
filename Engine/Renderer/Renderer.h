#pragma once

#include "Renderer/RenderSettings.h"

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

            void DrawFrame(float deltaTime);
            void WaitIdle();
            void EnableGui();
            void BeginGuiFrame();
            void EndGuiFrame();
            bool WantsGuiMouseInput() const;
            bool WantsGuiKeyboardInput() const;
            RenderSettings& GetSettings() { return m_Settings; }
            const RenderSettings& GetSettings() const { return m_Settings; }

        private:
            RenderSettings m_Settings;
            std::unique_ptr<VulkanContext> m_Context;
    };
}
