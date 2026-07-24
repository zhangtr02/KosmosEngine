#pragma once

#include <glm/glm.hpp>

namespace Kosmos
{
    class Camera
    {
        public:
            Camera(const glm::vec3& position, float yaw, float pitch);

            glm::mat4 GetViewMatrix() const;
            glm::mat4 GetProjectionMatrix(float aspectRatio) const;

            glm::vec3 GetForward() const;
            glm::vec3 GetRight() const;
            glm::vec3 GetUp() const;

            const glm::vec3& GetPosition() const { return m_Position; }

            void Move(const glm::vec3& offset);
            void Rotate(float yawOffset, float pitchOffset);

        private:
            glm::vec3 m_Position;
            float m_Yaw = 0.0f;
            float m_Pitch = 0.0f;
            float m_FieldOfView = glm::radians(45.0f);
            float m_NearPlane = 0.1f;
            float m_FarPlane = 100.0f;
    };
}