#pragma once

#include "Renderer/Vertex.h"

#include <cstdint>
#include <vector>

namespace Kosmos
{
    struct SceneGeometry
    {
        std::vector<Vertex> vertices;
        std::vector<uint16_t> indices;
    };

    SceneGeometry CreateDemoSceneGeometry();
}