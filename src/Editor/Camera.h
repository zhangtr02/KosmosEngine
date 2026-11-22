#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

struct GLFWwindow;

class Camera
{
  public:
    Camera();
    void frameModel(glm::vec3 center, float radius, float aspect);
    void update(GLFWwindow* window, float deltaTime, bool captureMouse = false,
                bool captureKeyboard = false);
    glm::mat4 view() const;
    glm::vec3 position() const { return position_; }
    float nearPlane() const { return nearPlane_; }
    bool looking() const { return looking_; }

  private:
    glm::vec3 forward() const;

    glm::vec3 position_{2.0f, 2.0f, 3.0f};
    float yaw_{};
    float pitch_{};
    float nearPlane_ = 0.05f;
    float speed_ = 2.0f;
    double cursorX_{};
    double cursorY_{};
    bool looking_ = false;
};
