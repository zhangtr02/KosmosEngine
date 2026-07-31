#pragma once

#include <cstdint>

namespace Kosmos
{
    enum class RenderDebugView : uint32_t
    {
        FinalColor = 0,
        Albedo = 1,
        WorldNormal = 2,
        Roughness = 3,
        Metallic = 4,
        Depth = 5,
        RawAmbientOcclusion = 6,
        AmbientOcclusion = 7,
        SceneColor = 8,
        Bloom = 9
    };

    struct BloomSettings
    {
        float threshold = 1.0f;
        float knee = 0.5f;
        float intensity = 0.08f;
    };

    struct AutomaticExposureSettings
    {
        float minimum = 0.05f;
        float maximum = 8.0f;
        float increaseSpeed = 1.5f;
        float decreaseSpeed = 3.0f;
    };

    struct SSAOSettings
    {
        float radius = 0.75f;
        float bias = 0.025f;
        float power = 1.5f;
        float depthSharpness = 4.0f;
        float normalSharpness = 16.0f;
    };

    struct EnvironmentSettings
    {
        uint32_t prefilterResolution = 128;
        uint32_t prefilterSampleCount = 512;
    };

    struct RenderSettings
    {
        RenderDebugView debugView = RenderDebugView::FinalColor;
        float exposureCompensation = 1.0f;
        BloomSettings bloom;
        AutomaticExposureSettings automaticExposure;
        SSAOSettings ssao;
        EnvironmentSettings environment;
    };
}
