#include "Camera.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
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

void Camera::frameModel(glm::vec3 center, float radius, float aspect)
{
    // Fit the model's bounding sphere to the narrower field of view.
    const float halfFov = glm::atan(glm::tan(glm::radians(22.5f)) * std::min(aspect, 1.0f));
    const Camera initial;
    yaw_ = initial.yaw_;
    pitch_ = initial.pitch_;
    panDistance_ = 1.15f * radius / glm::sin(halfFov);
    position_ = center - forward() * panDistance_;
    nearPlane_ = std::max(0.00001f, radius * 0.025f);
    baseSpeed_ = std::max(0.001f, radius * 2.0f);
}

glm::mat4 Camera::view() const
{
    return glm::lookAt(position_, position_ + forward(), glm::vec3(0.0f, 1.0f, 0.0f));
}

void Camera::translate(glm::vec3 displacement)
{
    position_ += displacement;
    panDistance_ = std::max(nearPlane_ * 4.0f, panDistance_ - glm::dot(displacement, forward()));
}

void Camera::update(GLFWwindow* window, float deltaTime, float panPixelScale, float wheel,
                    bool captureMouse, bool captureKeyboard)
{
    const bool focused =
        glfwGetWindowAttrib(window, GLFW_FOCUSED) && !glfwGetWindowAttrib(window, GLFW_ICONIFIED);
    auto navigation = Navigation::None;
    if (focused && !captureMouse)
    {
        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)
            navigation = Navigation::Look;
        else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS)
            navigation = Navigation::Pan;
    }
    const bool wasNavigating = navigating();
    if (navigation != navigation_)
    {
        const bool capture = navigation != Navigation::None;
        if (capture != wasNavigating)
            glfwSetInputMode(window, GLFW_CURSOR,
                             capture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        glfwGetCursorPos(window, &cursorX_, &cursorY_);
        navigation_ = navigation;
    }
    if (!navigating())
    {
        if (focused && !captureMouse && !wasNavigating && wheel != 0.0f)
        {
            const float step = std::min(baseSpeed_ * 0.25f, panDistance_ * 0.2f);
            translate(forward() * (wheel * step));
        }
        return;
    }

    double x, y;
    glfwGetCursorPos(window, &x, &y);
    const float dx = static_cast<float>(x - cursorX_), dy = static_cast<float>(y - cursorY_);
    cursorX_ = x;
    cursorY_ = y;

    if (navigation_ == Navigation::Pan)
    {
        const auto forwardDirection = forward();
        const auto right =
            glm::normalize(glm::cross(forwardDirection, glm::vec3(0.0f, 1.0f, 0.0f)));
        const auto up = glm::cross(right, forwardDirection);
        position_ += (-right * dx + up * dy) * (panDistance_ * panPixelScale);
        return;
    }

    yaw_ += dx * 0.1f;
    pitch_ = std::clamp(pitch_ - dy * 0.1f, -89.0f, 89.0f);
    if (wheel != 0.0f)
        flySpeedMultiplier_ =
            std::clamp(flySpeedMultiplier_ * std::exp2(wheel * 0.25f), 0.0625f, 16.0f);

    if (captureKeyboard)
        return;
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
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
        movement.y -= 1.0f;
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
        movement.y += 1.0f;
    if (glm::dot(movement, movement) > 0.0f)
        translate(glm::normalize(movement) * (baseSpeed_ * flySpeedMultiplier_ * deltaTime));
}
