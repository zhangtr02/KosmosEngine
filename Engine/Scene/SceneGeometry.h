#pragma once

#include "Renderer/Vertex.h"
#include "Scene/Transform.h"

#include <cstdint>
#include <vector>

namespace Kosmos
{
    struct SceneGeometry
    {
        std::vector<Vertex> vertices;
        std::vector<uint16_t> indices;
    };

    struct DemoScene
    {
        SceneGeometry geometry;
        std::vector<Transform> objectTransforms;
    };

    DemoScene CreateDemoScene();
}