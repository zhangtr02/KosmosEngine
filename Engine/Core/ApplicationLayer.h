#pragma once

namespace Kosmos
{
    class Renderer;
    class Scene;

    class ApplicationLayer
    {
        public:
            virtual ~ApplicationLayer() = default;

            virtual void OnAttach(Renderer& renderer, Scene& scene) {}
            virtual void OnUpdate(float deltaTime) {}
            virtual void OnGui() {}
    };
}