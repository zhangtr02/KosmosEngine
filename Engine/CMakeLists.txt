add_library(KosmosEngine STATIC)

target_sources(KosmosEngine
    PRIVATE
        Core/Application.cpp
        Core/Window.cpp
        Core/Input.cpp

        Renderer/Renderer.cpp
        Renderer/Resources/Mesh.cpp
        Renderer/Resources/Model.cpp
        Renderer/Resources/Material.cpp
        Renderer/Resources/Texture.cpp
        Renderer/Resources/CubeTexture.cpp

        Renderer/Loaders/ObjLoader.cpp
        Renderer/Loaders/TextureLoader.cpp

        Renderer/Lighting/EnvironmentLighting.cpp
        Renderer/Lighting/BrdfLutGenerator.cpp

        Renderer/Vulkan/VulkanContext.cpp

        Renderer/Vulkan/Core/VulkanInstance.cpp
        Renderer/Vulkan/Core/VulkanSurface.cpp
        Renderer/Vulkan/Core/VulkanDevice.cpp
        Renderer/Vulkan/Core/VulkanFrameContext.cpp

        Renderer/Vulkan/Presentation/VulkanSwapchain.cpp
        Renderer/Vulkan/Presentation/VulkanPresentPass.cpp

        Renderer/Vulkan/Resources/VulkanBuffer.cpp
        Renderer/Vulkan/Resources/VulkanImage.cpp
        Renderer/Vulkan/Resources/VulkanTexture.cpp
        Renderer/Vulkan/Resources/VulkanCubeTexture.cpp
        Renderer/Vulkan/Resources/VulkanMesh.cpp
        Renderer/Vulkan/Resources/VulkanMaterial.cpp

        Renderer/Vulkan/Descriptors/VulkanDescriptorSetLayout.cpp
        Renderer/Vulkan/Descriptors/VulkanDescriptorPool.cpp
        Renderer/Vulkan/Descriptors/VulkanDescriptorWriter.cpp

        Renderer/Vulkan/Pipelines/VulkanGraphicsPipeline.cpp
        Renderer/Vulkan/Pipelines/VulkanComputePipeline.cpp

        Renderer/Vulkan/RenderTargets/VulkanDepthBuffer.cpp
        Renderer/Vulkan/RenderTargets/VulkanRenderTarget.cpp
        Renderer/Vulkan/RenderTargets/VulkanGBuffer.cpp

        Renderer/Vulkan/Scene/VulkanSceneRenderer.cpp
        Renderer/Vulkan/View/VulkanRenderView.cpp

        Renderer/Vulkan/Passes/VulkanFullscreenPass.cpp
        Renderer/Vulkan/Passes/VulkanDirectionalShadowPass.cpp
        Renderer/Vulkan/Passes/VulkanSkyboxPass.cpp
        Renderer/Vulkan/Passes/VulkanEnvironmentPrefilter.cpp
        Renderer/Vulkan/Passes/VulkanBloomPass.cpp
        Renderer/Vulkan/Passes/VulkanAutoExposurePass.cpp
        Renderer/Vulkan/Passes/VulkanDeferredLightingPass.cpp
        Renderer/Vulkan/Passes/VulkanSSAOPass.cpp

        Scene/Scene.cpp
        Scene/DemoScene.cpp
        Scene/Transform.cpp
        Scene/Camera.cpp
        Scene/CameraController.cpp

    PUBLIC
        Core/Application.h
        Core/Window.h
        Core/Input.h

        Renderer/Renderer.h
        Renderer/RenderSettings.h

        Renderer/Resources/Vertex.h
        Renderer/Resources/Mesh.h
        Renderer/Resources/Model.h
        Renderer/Resources/Material.h
        Renderer/Resources/Texture.h
        Renderer/Resources/CubeTexture.h

        Renderer/Loaders/ObjLoader.h
        Renderer/Loaders/TextureLoader.h

        Renderer/Lighting/EnvironmentLighting.h
        Renderer/Lighting/BrdfLutGenerator.h

        Renderer/ShaderData/CameraUniform.h
        Renderer/ShaderData/LightingUniform.h
        Renderer/ShaderData/MaterialUniform.h
        Renderer/ShaderData/ObjectPushConstant.h

        Renderer/Vulkan/VulkanContext.h

        Renderer/Vulkan/Core/VulkanInstance.h
        Renderer/Vulkan/Core/VulkanSurface.h
        Renderer/Vulkan/Core/VulkanDevice.h
        Renderer/Vulkan/Core/VulkanFrameContext.h

        Renderer/Vulkan/Presentation/VulkanSwapchain.h
        Renderer/Vulkan/Presentation/VulkanPresentPass.h

        Renderer/Vulkan/Resources/VulkanBuffer.h
        Renderer/Vulkan/Resources/VulkanImage.h
        Renderer/Vulkan/Resources/VulkanTexture.h
        Renderer/Vulkan/Resources/VulkanCubeTexture.h
        Renderer/Vulkan/Resources/VulkanMesh.h
        Renderer/Vulkan/Resources/VulkanMaterial.h

        Renderer/Vulkan/Descriptors/VulkanDescriptorSetLayout.h
        Renderer/Vulkan/Descriptors/VulkanDescriptorPool.h
        Renderer/Vulkan/Descriptors/VulkanDescriptorWriter.h

        Renderer/Vulkan/Pipelines/VulkanGraphicsPipeline.h
        Renderer/Vulkan/Pipelines/VulkanGraphicsPipelineDescription.h
        Renderer/Vulkan/Pipelines/VulkanComputePipeline.h

        Renderer/Vulkan/RenderTargets/VulkanDepthBuffer.h
        Renderer/Vulkan/RenderTargets/VulkanRenderTarget.h
        Renderer/Vulkan/RenderTargets/VulkanRenderTargetDescription.h
        Renderer/Vulkan/RenderTargets/VulkanGBuffer.h

        Renderer/Vulkan/Scene/VulkanSceneRenderer.h
        Renderer/Vulkan/View/VulkanRenderView.h
        Renderer/Vulkan/View/VulkanRenderViewImages.h

        Renderer/Vulkan/Passes/VulkanFullscreenPass.h
        Renderer/Vulkan/Passes/VulkanDirectionalShadowPass.h
        Renderer/Vulkan/Passes/VulkanSkyboxPass.h
        Renderer/Vulkan/Passes/VulkanEnvironmentPrefilter.h
        Renderer/Vulkan/Passes/VulkanBloomPass.h
        Renderer/Vulkan/Passes/VulkanAutoExposurePass.h
        Renderer/Vulkan/Passes/VulkanDeferredLightingPass.h
        Renderer/Vulkan/Passes/VulkanSSAOPass.h

        Scene/Light.h
        Scene/Scene.h
        Scene/DemoScene.h
        Scene/Transform.h
        Scene/Camera.h
        Scene/CameraController.h
)

target_include_directories(KosmosEngine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(KosmosEngine
    PRIVATE
        stb_image

    PUBLIC
        Vulkan::Vulkan
        glfw
        glm
)

target_compile_definitions(KosmosEngine
    PUBLIC
        GLFW_INCLUDE_VULKAN
        GLM_FORCE_RADIANS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
)
