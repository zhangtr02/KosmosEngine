#pragma once

#include <cstdint>
#include <memory>

namespace Kosmos
{
    class Texture;

    class BrdfLutGenerator
    {
        public:
            static std::shared_ptr<Texture> Generate(uint32_t resolution, uint32_t sampleCount);
    };
}