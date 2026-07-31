#include "Editor/EditorLayer.h"
#include "Renderer/Renderer.h"
#include "Scene/Scene.h"

#include <imgui.h>
#include <string>

namespace Kosmos::Sandbox
{
    void EditorLayer::OnAttach(Renderer& renderer, Scene& scene)
    {
        m_Renderer = &renderer;
        m_Scene = &scene;
    }

    void EditorLayer::OnGui()
    {
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("View"))
            {
                ImGui::MenuItem("Hierarchy", nullptr, &m_ShowHierarchy);
                ImGui::MenuItem("Inspector", nullptr, &m_ShowInspector);
                ImGui::MenuItem("Renderer", nullptr, &m_ShowRenderer);
                ImGui::MenuItem("ImGui Demo", nullptr, &m_ShowImGuiDemo);
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }

        if (m_ShowHierarchy)
        {
            DrawHierarchyPanel();
        }

        if (m_ShowInspector)
        {
            DrawInspectorPanel();
        }

        if (m_ShowRenderer)
        {
            DrawRendererPanel();
        }

        if (m_ShowImGuiDemo)
        {
            ImGui::ShowDemoWindow(&m_ShowImGuiDemo);
        }
    }

    void EditorLayer::DrawHierarchyPanel()
    {
        ImGui::Begin("Scene Hierarchy", &m_ShowHierarchy);

        const std::vector<RenderObject>& objects = m_Scene->GetRenderObjects();

        for (size_t objectIndex = 0; objectIndex < objects.size(); ++objectIndex)
        {
            const std::string label = "Render Object " + std::to_string(objectIndex);
            if (ImGui::Selectable(label.c_str(), objectIndex == m_SelectedObjectIndex))
            {
                m_SelectedObjectIndex = objectIndex;
            }
        }

        ImGui::End();
    }

    void EditorLayer::DrawInspectorPanel()
    {
        ImGui::Begin("Inspector", &m_ShowInspector);

        const std::vector<RenderObject>& objects = m_Scene->GetRenderObjects();

        if (m_SelectedObjectIndex < objects.size())
        {
            const Transform& transform = objects[m_SelectedObjectIndex].transform;
            ImGui::Text("Render Object %zu", m_SelectedObjectIndex);
            ImGui::Separator();
            ImGui::Text("Position: %.2f, %.2f, %.2f", transform.position.x, transform.position.y, transform.position.z);
            ImGui::Text("Rotation: %.2f, %.2f, %.2f", transform.rotation.x, transform.rotation.y, transform.rotation.z);
            ImGui::Text("Scale: %.2f, %.2f, %.2f", transform.scale.x, transform.scale.y, transform.scale.z);
        }
        else
        {
            ImGui::TextDisabled("No object selected");
        }

        ImGui::End();
    }

    void EditorLayer::DrawRendererPanel()
    {
        ImGui::Begin("Renderer", &m_ShowRenderer);

        RenderSettings& settings = m_Renderer->GetSettings();
        constexpr const char* debugViews[] = {"Final Color", "Albedo", "World Normal", "Roughness", "Metallic", "Depth", "Raw AO", "Ambient Occlusion", "Scene Color", "Bloom"};
        int debugView = static_cast<int>(settings.debugView);

        if (ImGui::Combo("Debug View", &debugView, debugViews, static_cast<int>(std::size(debugViews))))
        {
            settings.debugView = static_cast<RenderDebugView>(debugView);
        }

        ImGui::SliderFloat("Exposure Compensation", &settings.exposureCompensation, 0.1f, 4.0f);
        ImGui::SliderFloat("Bloom Threshold", &settings.bloom.threshold, 0.0f, 10.0f);
        ImGui::SliderFloat("Bloom Intensity", &settings.bloom.intensity, 0.0f, 1.0f);
        ImGui::SliderFloat("SSAO Radius", &settings.ssao.radius, 0.05f, 3.0f);
        ImGui::SliderFloat("SSAO Power", &settings.ssao.power, 0.1f, 5.0f);

        ImGui::End();
    }
}
