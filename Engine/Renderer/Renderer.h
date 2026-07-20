#pragma once

#include "Renderer/Vulkan/VulkanContext.h"

namespace Kosmos
{
    class Window;
    class VulkanContext;
    class Renderer
    {
        public:
            explicit Renderer(Window& window);
            ~Renderer();

            void DrawFrame();
            void WaitIdle();

        private:
            VulkanContext m_Context;
    };
}