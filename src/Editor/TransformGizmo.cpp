#include "TransformGizmo.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

namespace
{
constexpr int uniformAxis = 3;
constexpr int ringSegments = 64;
constexpr float ringStep = glm::two_pi<float>() / ringSegments;
constexpr std::array<ImU32, 4> colors{IM_COL32(244, 83, 82, 255), IM_COL32(105, 214, 116, 255),
                                      IM_COL32(92, 160, 255, 255), IM_COL32(221, 226, 235, 255)};
constexpr ImU32 highlight = IM_COL32(255, 220, 105, 255);

ImVec2 point(glm::vec2 value) { return {value.x, value.y}; }

std::optional<glm::vec2> project(const GizmoViewport& viewport, glm::vec3 position)
{
    const glm::vec3 camera(viewport.view * glm::vec4(position, 1.0f));
    if (-camera.z <= viewport.nearPlane)
        return {};
    const auto ndc =
        glm::vec2(camera.x / viewport.aspect, -camera.y) / (-camera.z * viewport.tanHalfFov);
    return viewport.origin + (ndc * 0.5f + 0.5f) * viewport.size;
}

struct Ray
{
    glm::vec3 origin;
    glm::vec3 direction;
};

Ray cursorRay(const GizmoViewport& viewport, glm::vec2 mouse)
{
    const auto ndc = (mouse - viewport.origin) / viewport.size * 2.0f - 1.0f;
    const auto inverse = glm::inverse(viewport.view);
    const glm::vec3 direction(ndc.x * viewport.aspect * viewport.tanHalfFov,
                              -ndc.y * viewport.tanHalfFov, -1.0f);
    return {glm::vec3(inverse[3]), glm::normalize(glm::mat3(inverse) * direction)};
}

std::optional<float> onAxis(const Ray& ray, glm::vec3 pivot, glm::vec3 axis)
{
    const float alignment = glm::dot(ray.direction, axis);
    const float denominator = 1.0f - alignment * alignment;
    if (denominator < 0.0025f)
        return {};
    const auto offset = ray.origin - pivot;
    return (glm::dot(axis, offset) - alignment * glm::dot(ray.direction, offset)) / denominator;
}

std::optional<glm::vec3> onRing(const Ray& ray, glm::vec3 pivot, glm::vec3 axis)
{
    const float denominator = glm::dot(ray.direction, axis);
    if (std::abs(denominator) < 0.0025f)
        return {};
    const auto offset = ray.origin - pivot;
    const float distance = -glm::dot(offset, axis) / denominator;
    if (distance <= 0.0f)
        return {};
    const auto radial = offset + ray.direction * distance;
    if (glm::length(radial) <= distance * 0.00001f)
        return {};
    return glm::normalize(radial);
}
} // namespace

void TransformGizmo::updateDrag(const GizmoViewport& view, glm::vec2 mouse, glm::vec3& position,
                                glm::vec3& rotation, glm::vec3& scale)
{
    const auto& drag = *drag_;
    if (mouse == drag.mouse)
    {
        position = drag.position;
        rotation = drag.rotation;
        scale = drag.scale;
        return;
    }
    const float screenDelta = glm::dot(mouse - drag.mouse, drag.screenDirection);
    if (mode_ == Mode::Move)
    {
        float distance = screenDelta * drag.worldLength / drag.pixels;
        if (drag.axisStart)
        {
            const auto current = onAxis(cursorRay(view, mouse), drag.position, drag.worldAxis);
            if (!current)
                return;
            distance = *current - *drag.axisStart;
        }
        position = drag.position + drag.worldAxis * distance;
    }
    else if (mode_ == Mode::Rotate)
    {
        float angle = screenDelta / drag.pixels;
        if (drag.radialStart)
        {
            const auto current = onRing(cursorRay(view, mouse), drag.position, drag.worldAxis);
            if (!current)
                return;
            angle = std::atan2(glm::dot(drag.worldAxis, glm::cross(*drag.radialStart, *current)),
                               glm::dot(*drag.radialStart, *current));
        }
        const auto initial = glm::quat(glm::radians(drag.rotation));
        const auto rotated = glm::angleAxis(angle, drag.worldAxis) * initial;
        rotation = glm::degrees(glm::eulerAngles(glm::normalize(rotated)));
    }
    else
    {
        constexpr float minScale = 0.001f, maxScale = 1000.0f;
        const float factor = std::exp2(std::clamp(screenDelta / drag.pixels, -20.0f, 20.0f));
        if (drag.axis == uniformAxis)
        {
            const float low = std::min({drag.scale.x, drag.scale.y, drag.scale.z});
            const float high = std::max({drag.scale.x, drag.scale.y, drag.scale.z});
            scale = drag.scale * std::clamp(factor, minScale / low, maxScale / high);
        }
        else
            scale[drag.axis] = std::clamp(drag.scale[drag.axis] * factor, minScale, maxScale);
    }
}

void TransformGizmo::updateTool(bool acceptsInput)
{
    const auto& io = ImGui::GetIO();
    if (!acceptsInput || drag_ || io.WantCaptureKeyboard || io.KeyCtrl || io.KeyAlt ||
        io.KeySuper || io.KeyShift)
        return;

    if (ImGui::IsKeyPressed(ImGuiKey_Q, false))
        mode_ = Mode::Cursor;
    else if (ImGui::IsKeyPressed(ImGuiKey_W, false))
        mode_ = Mode::Move;
    else if (ImGui::IsKeyPressed(ImGuiKey_E, false))
        mode_ = Mode::Rotate;
    else if (ImGui::IsKeyPressed(ImGuiKey_R, false))
        mode_ = Mode::Scale;
}

void TransformGizmo::edit(const GizmoViewport& view, bool acceptsInput, glm::vec3& position,
                          glm::vec3& rotation, glm::vec3& scale)
{
    const auto& io = ImGui::GetIO();
    const glm::vec2 mouse(io.MousePos.x, io.MousePos.y);
    bool cancelled = false;
    if (drag_)
    {
        if (io.AppFocusLost || !ImGui::IsMousePosValid(&io.MousePos) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            position = drag_->position;
            rotation = drag_->rotation;
            scale = drag_->scale;
            endDrag();
            cancelled = true;
        }
        else
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
                ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                updateDrag(view, mouse, position, rotation, scale);
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
                endDrag();
        }
    }
    constexpr const char* hints[]{
        "Q Cursor   [W] Move   E Rotate   R Scale", "Q Cursor   W Move   [E] Rotate   R Scale",
        "Q Cursor   W Move   E Rotate   [R] Scale", "[Q] Cursor   W Move   E Rotate   R Scale"};
    auto* draw = ImGui::GetBackgroundDrawList();
    draw->PushClipRect(point(view.origin), point(view.origin + view.size), true);
    draw->AddText(point(view.origin + glm::vec2(8.0f * view.uiScale)),
                  ImGui::GetColorU32(ImGuiCol_TextDisabled), hints[static_cast<int>(mode_)]);
    const auto center = mode_ == Mode::Cursor ? std::nullopt : project(view, position);
    if (!center)
    {
        draw->PopClipRect();
        return;
    }
    const float depth = -(view.view * glm::vec4(position, 1.0f)).z;
    const float pixels = 72.0f * view.uiScale;
    const float length = 2.0f * depth * view.tanHalfFov * pixels / view.size.y;
    const auto axes =
        mode_ == Mode::Scale ? glm::mat3_cast(glm::quat(glm::radians(rotation))) : glm::mat3(1.0f);
    const bool rings = mode_ == Mode::Rotate;
    const int segments = rings ? ringSegments : 1;
    std::array<std::array<std::optional<glm::vec2>, ringSegments + 1>, 3> lines{};
    for (int axis = 0; axis < 3; ++axis)
    {
        if (rings)
            for (int i = 0; i <= segments; ++i)
            {
                const float angle = float(i) * ringStep;
                const auto radial =
                    axes[(axis + 1) % 3] * std::cos(angle) + axes[(axis + 2) % 3] * std::sin(angle);
                lines[axis][i] = project(view, position + radial * length);
            }
        else if (const auto tip = project(view, position + axes[axis] * length);
                 tip && glm::length(*tip - *center) >= 12.0f * view.uiScale)
        {
            lines[axis][0] = center;
            lines[axis][1] = tip;
        }
    }

    int hovered = -1;
    glm::vec2 dragDirection(1.0f, 0.0f);
    float dragPixels = pixels;
    const float centerSize = 7.0f * view.uiScale;
    if (acceptsInput && !drag_ && !cancelled)
    {
        const auto fromCenter = glm::abs(mouse - *center);
        if (mode_ == Mode::Scale && fromCenter.x <= centerSize && fromCenter.y <= centerSize)
            hovered = uniformAxis;
        else
        {
            float best = 8.0f * view.uiScale;
            for (int axis = 0; axis < 3; ++axis)
                for (int i = 0; i < segments; ++i)
                {
                    const auto& a = lines[axis][i];
                    const auto& b = lines[axis][i + 1];
                    if (!a || !b)
                        continue;
                    const auto delta = *b - *a;
                    const float size = glm::length(delta);
                    if (size < 0.01f)
                        continue;
                    const auto direction = delta / size;
                    const float along = glm::dot(mouse - *a, direction);
                    const float distance = glm::length(
                        mouse - *a -
                        direction * std::clamp(along, rings ? 0.0f : size * 0.16f, size));
                    if (distance >= best)
                        continue;
                    best = distance;
                    hovered = axis;
                    dragDirection = direction;
                    dragPixels = rings ? std::max(size / ringStep, 18.0f * view.uiScale) : size;
                }
        }
        if (hovered >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            const auto axis = hovered == uniformAxis ? glm::vec3(0.0f) : axes[hovered];
            const auto ray = cursorRay(view, mouse);
            drag_ = Drag{hovered,  mouse,    dragDirection, dragPixels, length, axis,
                         position, rotation, scale,         {},         {}};
            if (mode_ == Mode::Move)
                drag_->axisStart = onAxis(ray, position, axis);
            // Use screen-space tangent motion for rings viewed nearly edge-on.
            if (rings && std::abs(glm::dot(glm::normalize(position - ray.origin), axis)) > 0.12f)
                drag_->radialStart = onRing(ray, position, axis);
        }
    }

    const int active = drag_ ? drag_->axis : hovered;
    for (int axis = 0; axis < 3; ++axis)
    {
        const auto color = active == axis ? highlight : colors[axis];
        const float lineWidth = (active == axis ? 3.5f : 2.5f) * view.uiScale;
        for (int i = 0; i < segments; ++i)
            if (lines[axis][i] && lines[axis][i + 1])
                draw->AddLine(point(*lines[axis][i]), point(*lines[axis][i + 1]), color, lineWidth);
        if (rings || !lines[axis][1])
            continue;
        const auto tip = *lines[axis][1];
        if (mode_ == Mode::Move)
        {
            const auto direction = glm::normalize(tip - *center);
            const auto base = tip - direction * (11.0f * view.uiScale);
            const auto side = glm::vec2(-direction.y, direction.x) * (5.0f * view.uiScale);
            draw->AddTriangleFilled(point(tip), point(base + side), point(base - side), color);
        }
        else
        {
            const glm::vec2 half(5.0f * view.uiScale);
            draw->AddRectFilled(point(tip - half), point(tip + half), color);
        }
    }
    if (mode_ == Mode::Scale)
    {
        const glm::vec2 half(centerSize);
        draw->AddRectFilled(point(*center - half), point(*center + half),
                            active == uniformAxis ? highlight : colors[uniformAxis]);
    }
    draw->PopClipRect();
}
