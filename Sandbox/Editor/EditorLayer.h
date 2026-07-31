#pragma once

#include "Core/ApplicationLayer.h"

#include <cstddef>

namespace Kosmos
{
    class Renderer;
    class Scene;
}

namespace Kosmos::Sandbox
{
    class EditorLayer final : public ApplicationLayer
    {
        public:
            void OnAttach(Renderer& renderer, Scene& scene) override;
            void OnGui() override;

        private:
            void DrawHierarchyPanel();
            void DrawInspectorPanel();
            void DrawRendererPanel();

        private:
            Renderer* m_Renderer = nullptr;
            Scene* m_Scene = nullptr;
            size_t m_SelectedObjectIndex = 0;
            bool m_ShowHierarchy = true;
            bool m_ShowInspector = true;
            bool m_ShowRenderer = true;
            bool m_ShowImGuiDemo = false;
    };
}