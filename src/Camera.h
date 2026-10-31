#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

struct GLFWwindow;

class Camera
{
  public:
    Camera();
    void update(GLFWwindow* window, float deltaTime);
    glm::mat4 view() const;

  private:
    glm::vec3 forward() const;

    glm::vec3 position_{2.0f, 2.0f, 3.0f};
    float yaw_{};
    float pitch_{};
    double cursorX_{};
    double cursorY_{};
    bool looking_ = false;
};
