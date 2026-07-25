#pragma once

#include <memory>

namespace Kosmos
{
    class Scene;

    std::unique_ptr<Scene> CreateDemoScene();
}