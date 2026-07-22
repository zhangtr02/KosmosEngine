add_library(KosmosEngine STATIC)

target_sources(KosmosEngine
    PRIVATE
        Core/Application.cpp
        Core/Window.cpp
        Renderer/Renderer.cpp
        Renderer/Vulkan/VulkanInstance.cpp
        Renderer/Vulkan/VulkanSurface.cpp
        Renderer/Vulkan/VulkanDevice.cpp
        Renderer/Vulkan/VulkanSwapchain.cpp
        Renderer/Vulkan/VulkanPipeline.cpp
        Renderer/Vulkan/VulkanFrameContext.cpp
        Renderer/Vulkan/VulkanContext.cpp

    PUBLIC
        Core/Application.h
        Core/Window.h
        Renderer/Vulkan/VulkanInstance.h
        Renderer/Vulkan/VulkanSurface.h
        Renderer/Vulkan/VulkanDevice.h
        Renderer/Vulkan/VulkanSwapchain.h
        Renderer/Vulkan/VulkanPipeline.h
        Renderer/Vulkan/VulkanFrameContext.h
        Renderer/Vulkan/VulkanContext.h
)

target_include_directories(KosmosEngine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(KosmosEngine
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