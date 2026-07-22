#include "Renderer/Renderer.h"
#include "Renderer/Vulkan/VulkanContext.h"

#include <memory>

namespace Kosmos
{
    Renderer::Renderer(Window& window)
    {
        m_Context = std::make_unique<VulkanContext>(window);
    }

    Renderer::~Renderer() = default;

    void Renderer::DrawFrame()
    {
        m_Context->DrawFrame();
    }

    void Renderer::WaitIdle()
    {
        m_Context->WaitIdle();
    }
}