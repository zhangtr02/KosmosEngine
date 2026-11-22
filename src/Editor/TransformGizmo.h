#pragma once

#include "Viewport.h"

class TransformGizmo
{
  public:
    void updateTool(bool acceptsInput);
    void edit(const EditorViewport& view, bool acceptsInput, glm::vec3& position,
              glm::vec3& rotation, glm::vec3& scale);
    bool dragging() const { return drag_.has_value(); }
    void endDrag() { drag_.reset(); }

  private:
    enum class Mode
    {
        Translate,
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

    void updateDrag(const EditorViewport& view, glm::vec2 mouse, glm::vec3& position,
                    glm::vec3& rotation, glm::vec3& scale);

    Mode mode_ = Mode::Translate;
    std::optional<Drag> drag_;
};
