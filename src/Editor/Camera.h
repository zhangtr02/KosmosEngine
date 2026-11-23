#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

struct GLFWwindow;

class Camera
{
  public:
    Camera();
    void frameModel(glm::vec3 center, float radius, float aspect);
    void update(GLFWwindow* window, float deltaTime, float panPixelScale, float wheel,
                bool captureMouse, bool captureKeyboard);
    glm::mat4 view() const;
    glm::vec3 position() const { return position_; }
    float nearPlane() const { return nearPlane_; }
    bool navigating() const { return navigation_ != Navigation::None; }

  private:
    enum class Navigation
    {
        None,
        Look,
        Pan
    };

    glm::vec3 forward() const;
    void translate(glm::vec3 displacement);

    glm::vec3 position_{2.0f, 2.0f, 3.0f};
    float yaw_{};
    float pitch_{};
    float nearPlane_ = 0.05f;
    float baseSpeed_ = 2.0f;
    float flySpeedMultiplier_ = 1.0f;
    float panDistance_ = 4.0f;
    double cursorX_{};
    double cursorY_{};
    Navigation navigation_ = Navigation::None;
};
