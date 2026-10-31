#include "Camera.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

Camera::Camera()
{
    const auto direction = glm::normalize(-position_);
    yaw_ = glm::degrees(glm::atan(direction.z, direction.x));
    pitch_ = glm::degrees(glm::asin(direction.y));
}

glm::vec3 Camera::forward() const
{
    const float yaw = glm::radians(yaw_);
    const float pitch = glm::radians(pitch_);
    return {glm::cos(pitch) * glm::cos(yaw), glm::sin(pitch), glm::cos(pitch) * glm::sin(yaw)};
}

glm::mat4 Camera::view() const
{
    return glm::lookAt(position_, position_ + forward(), glm::vec3(0.0f, 1.0f, 0.0f));
}

void Camera::update(GLFWwindow* window, float deltaTime)
{
    const bool focused =
        glfwGetWindowAttrib(window, GLFW_FOCUSED) && !glfwGetWindowAttrib(window, GLFW_ICONIFIED);
    const bool looking =
        focused && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    if (looking != looking_)
    {
        glfwSetInputMode(window, GLFW_CURSOR, looking ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        glfwGetCursorPos(window, &cursorX_, &cursorY_);
        looking_ = looking;
    }
    if (!focused)
        return;

    if (looking_)
    {
        double x, y;
        glfwGetCursorPos(window, &x, &y);
        yaw_ += static_cast<float>(x - cursorX_) * 0.1f;
        pitch_ = std::clamp(pitch_ - static_cast<float>(y - cursorY_) * 0.1f, -89.0f, 89.0f);
        cursorX_ = x;
        cursorY_ = y;
    }

    const auto front = forward();
    const auto right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
    glm::vec3 movement(0.0f);
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        movement += front;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        movement -= front;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        movement += right;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        movement -= right;
    if (glm::dot(movement, movement) > 0.0f)
        position_ += glm::normalize(movement) * 2.0f * deltaTime;
}
