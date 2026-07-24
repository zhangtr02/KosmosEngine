#include "Core/Input.h"
#include "Core/Window.h"

#include <GLFW/glfw3.h>

namespace
{
    int ToGlfwKey(Kosmos::Key key)
    {
        switch (key)
        {
            case Kosmos::Key::W: return GLFW_KEY_W;
            case Kosmos::Key::A: return GLFW_KEY_A;
            case Kosmos::Key::S: return GLFW_KEY_S;
            case Kosmos::Key::D: return GLFW_KEY_D;
            case Kosmos::Key::Q: return GLFW_KEY_Q;
            case Kosmos::Key::E: return GLFW_KEY_E;
            case Kosmos::Key::LeftShift: return GLFW_KEY_LEFT_SHIFT;
        }

        return GLFW_KEY_UNKNOWN;
    }

    int ToGlfwMouseButton(Kosmos::MouseButton button)
    {
        switch (button)
        {
            case Kosmos::MouseButton::Right: return GLFW_MOUSE_BUTTON_RIGHT;
        }

        return GLFW_MOUSE_BUTTON_LAST + 1;
    }
}

namespace Kosmos
{
    Input::Input(Window& window)
        : m_Window(window)
    {
    }

    bool Input::IsKeyDown(Key key) const
    {
        const int state = glfwGetKey(m_Window.GetNativeWindow(), ToGlfwKey(key));
        return state == GLFW_PRESS || state == GLFW_REPEAT;
    }

    bool Input::IsMouseButtonDown(MouseButton button) const
    {
        return glfwGetMouseButton(m_Window.GetNativeWindow(), ToGlfwMouseButton(button)) == GLFW_PRESS;
    }

    CursorPosition Input::GetCursorPosition() const
    {
        CursorPosition position;
        glfwGetCursorPos(m_Window.GetNativeWindow(), &position.x, &position.y);
        return position;
    }

    void Input::SetCursorCaptured(bool captured) const
    {
        GLFWwindow* window = m_Window.GetNativeWindow();

        glfwSetInputMode(window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);

        if (glfwRawMouseMotionSupported())
        {
            glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, captured ? GLFW_TRUE : GLFW_FALSE);
        }
    }
}