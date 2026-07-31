#include "Core/Application.h"
#include "Scene/Camera/Camera.h"
#include "Scene/Scene.h"
#include "Editor/EditorLayer.h"
#include "Scenes/DemoScene.h"

#include <glm/glm.hpp>
#include <iostream>
#include <memory>
#include <utility>

int main()
{
    try
    {
        std::unique_ptr<Kosmos::Scene> scene = Kosmos::Sandbox::CreateDemoScene();
        auto camera = std::make_unique<Kosmos::Camera>(glm::vec3(3.6f, 2.7f, 5.0f), glm::radians(-126.0f), glm::radians(-22.0f));
        auto editorLayer = std::make_unique<Kosmos::Sandbox::EditorLayer>();
        Kosmos::Application application(std::move(scene), std::move(camera), std::move(editorLayer));
        application.Run();
    }
    catch (const std::exception& exception)
    {
        std::cerr << "[Fatal Error] " << exception.what() << '\n';
        return 1;
    }

    return 0;
}