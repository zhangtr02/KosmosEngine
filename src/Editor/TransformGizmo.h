#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <optional>

struct GizmoViewport
{
    glm::mat4 view;
    glm::vec2 origin;
    glm::vec2 size;
    float aspect;
    float tanHalfFov;
    float nearPlane;
    float uiScale;
};

class TransformGizmo
{
  public:
    void updateTool(bool acceptsInput);
    void edit(const GizmoViewport& view, bool acceptsInput, glm::vec3& position,
              glm::vec3& rotation, glm::vec3& scale);
    bool dragging() const { return drag_.has_value(); }
    void endDrag() { drag_.reset(); }

  private:
    enum class Mode
    {
        Move,
        Rotate,
        Scale,
        Cursor
    };
    struct Drag
    {
        int axis;
        glm::vec2 mouse;
        glm::vec2 screenDirection;
        float pixels;
        float worldLength;
        glm::vec3 worldAxis;
        glm::vec3 position;
        glm::vec3 rotation;
        glm::vec3 scale;
        std::optional<float> axisStart;
        std::optional<glm::vec3> radialStart;
    };

    void updateDrag(const GizmoViewport& view, glm::vec2 mouse, glm::vec3& position,
                    glm::vec3& rotation, glm::vec3& scale);

    Mode mode_ = Mode::Move;
    std::optional<Drag> drag_;
};
