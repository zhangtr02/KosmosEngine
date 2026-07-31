#pragma once

namespace Kosmos
{
    class Input;
    class Camera;

    class CameraController
    {
        public:
            CameraController(Input& input, Camera& camera);

            void Update(float deltaTime);

        private:
            Input& m_Input;
            Camera& m_Camera;

            float m_MoveSpeed = 2.5f;
            float m_SprintMultiplier = 2.5f;
            float m_MouseSensitivity;

            bool m_IsRotating = false;
            double m_LastCursorX = 0.0;
            double m_LastCursorY = 0.0;
    };
}