#include "Renderer/Renderer.h"
#include "Renderer/Vulkan/VulkanContext.h"
#include "Scene/Camera.h"

#include <memory>

namespace Kosmos
{
    Renderer::Renderer(Window& window, const Camera& camera)
    {
        m_Context = std::make_unique<VulkanContext>(window, camera);
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