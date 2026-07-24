#include "Scene/CameraController.h"
#include "Scene/Camera.h"
#include "Core/Input.h"

#include <glm/glm.hpp>
#include <algorithm>

namespace Kosmos
{
    CameraController::CameraController(Input& input, Camera& camera)
        : m_Input(input), m_Camera(camera), m_MouseSensitivity(glm::radians(0.12f))
    {
    }

    void CameraController::Update(float deltaTime)
    {
        const float frameTime = std::clamp(deltaTime, 0.0f, 0.1f);
        const float speedMultiplier = m_Input.IsKeyDown(Key::LeftShift) ? m_SprintMultiplier : 1.0f;
        const float movementDistance = m_MoveSpeed * speedMultiplier * frameTime;

        glm::vec3 movement{0.0f};

        if (m_Input.IsKeyDown(Key::W))
        {
            movement += m_Camera.GetForward();
        }

        if (m_Input.IsKeyDown(Key::S))
        {
            movement -= m_Camera.GetForward();
        }

        if (m_Input.IsKeyDown(Key::D))
        {
            movement += m_Camera.GetRight();
        }

        if (m_Input.IsKeyDown(Key::A))
        {
            movement -= m_Camera.GetRight();
        }

        if (m_Input.IsKeyDown(Key::E))
        {
            movement += glm::vec3(0.0f, 1.0f, 0.0f);
        }

        if (m_Input.IsKeyDown(Key::Q))
        {
            movement -= glm::vec3(0.0f, 1.0f, 0.0f);
        }

        if (glm::dot(movement, movement) > 0.0f)
        {
            m_Camera.Move(glm::normalize(movement) * movementDistance);
        }

        if (m_Input.IsMouseButtonDown(MouseButton::Right))
        {
            if (!m_IsRotating)
            {
                m_Input.SetCursorCaptured(true);

                const CursorPosition position = m_Input.GetCursorPosition();
                m_LastCursorX = position.x;
                m_LastCursorY = position.y;
                m_IsRotating = true;
                return;
            }

            const CursorPosition position = m_Input.GetCursorPosition();
            const float deltaX = static_cast<float>(position.x - m_LastCursorX);
            const float deltaY = static_cast<float>(position.y - m_LastCursorY);

            m_LastCursorX = position.x;
            m_LastCursorY = position.y;

            m_Camera.Rotate(deltaX * m_MouseSensitivity, -deltaY * m_MouseSensitivity);
        }
        else if (m_IsRotating)
        {
            m_Input.SetCursorCaptured(false);
            m_IsRotating = false;
        }
    }
}