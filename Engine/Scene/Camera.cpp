#include "Scene/Camera.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace
{
    constexpr glm::vec3 WorldUp{0.0f, 1.0f, 0.0f};
    constexpr float MaximumPitch = glm::radians(89.0f);
}

namespace Kosmos
{
    Camera::Camera(const glm::vec3& position, float yaw, float pitch)
        : m_Position(position), m_Yaw(yaw), m_Pitch(std::clamp(pitch, -MaximumPitch, MaximumPitch))
    {
    }

    glm::mat4 Camera::GetViewMatrix() const
    {
        return glm::lookAt(m_Position, m_Position + GetForward(), GetUp());
    }

    glm::mat4 Camera::GetProjectionMatrix(float aspectRatio) const
    {
        return glm::perspective(m_FieldOfView, aspectRatio, m_NearPlane, m_FarPlane);
    }

    glm::vec3 Camera::GetForward() const
    {
        const float cosPitch = std::cos(m_Pitch);

        return glm::normalize(glm::vec3(
            std::cos(m_Yaw) * cosPitch,
            std::sin(m_Pitch),
            std::sin(m_Yaw) * cosPitch));
    }

    glm::vec3 Camera::GetRight() const
    {
        return glm::normalize(glm::cross(GetForward(), WorldUp));
    }

    glm::vec3 Camera::GetUp() const
    {
        return glm::normalize(glm::cross(GetRight(), GetForward()));
    }

    void Camera::Move(const glm::vec3& offset)
    {
        m_Position += offset;
    }

    void Camera::Rotate(float yawOffset, float pitchOffset)
    {
        m_Yaw += yawOffset;
        m_Pitch = std::clamp(m_Pitch + pitchOffset, -MaximumPitch, MaximumPitch);
    }
}