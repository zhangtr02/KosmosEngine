#pragma once

#include <memory>
#include <vector>

namespace Kosmos
{
    class Mesh;
    class Material;

    struct ModelPart
    {
        std::shared_ptr<Mesh> mesh;
        std::shared_ptr<Material> material;
    };

    class Model
    {
        public:
            explicit Model(std::vector<ModelPart> parts);

            const std::vector<ModelPart>& GetParts() const { return m_Parts; }

        private:
            std::vector<ModelPart> m_Parts;
    };
}