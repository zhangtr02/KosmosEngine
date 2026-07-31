#include "Renderer/Renderer.h"
#include "Renderer/Vulkan/VulkanContext.h"
#include "Scene/Camera.h"
#include "Scene/Scene.h"

#include <memory>

namespace Kosmos
{
    Renderer::Renderer(Window& window, const Camera& camera, const Scene& scene)
    {
        m_Context = std::make_unique<VulkanContext>(window, camera, scene, m_Settings);
    }

    Renderer::~Renderer() = default;

    void Renderer::DrawFrame(float deltaTime)
    {
        m_Context->DrawFrame(deltaTime);
    }

    void Renderer::WaitIdle()
    {
        m_Context->WaitIdle();
    }

}
