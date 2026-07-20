#include "Renderer/Renderer.h"

namespace Kosmos
{
    Renderer::Renderer(Window& window)
        : m_Context(window)
    {

    }

    Renderer::~Renderer()
    {
    }

    void Renderer::DrawFrame()
    {
        m_Context.DrawFrame();
    }

    void Renderer::WaitIdle()
    {
        m_Context.WaitIdle();
    }
}