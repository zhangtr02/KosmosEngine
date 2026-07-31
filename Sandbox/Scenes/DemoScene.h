#pragma once

#include <memory>

namespace Kosmos
{
    class Scene;
}

namespace Kosmos::Sandbox
{
    std::unique_ptr<Scene> CreateDemoScene();
}
