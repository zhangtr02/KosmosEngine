cmake_minimum_required(VERSION 4.2)
project(KosmosEngine LANGUAGES CXX)

find_package(Vulkan 1.3 REQUIRED)
find_package(basisu CONFIG REQUIRED)
find_package(draco CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(mikktspace CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(WebP CONFIG REQUIRED)
find_path(CGLTF_INCLUDE_DIR NAMES cgltf.h REQUIRED)
find_program(SLANGC_EXECUTABLE NAMES slangc
    HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)

add_library(KosmosRenderer STATIC
    src/Renderer/VulkanRenderer.cpp src/Renderer/VulkanRenderer.h
    src/Assets/Model.cpp src/Assets/Model.h
    src/Assets/Scene.cpp src/Assets/Scene.h
    src/Assets/Texture.cpp src/Assets/Texture.h
    3rdparty/cgltf.cpp 3rdparty/stb_image.cpp)
target_include_directories(KosmosRenderer PUBLIC src/Renderer src/Assets)
target_include_directories(KosmosRenderer SYSTEM PRIVATE ${Stb_INCLUDE_DIR} ${CGLTF_INCLUDE_DIR})
target_compile_features(KosmosRenderer PUBLIC cxx_std_20)
target_compile_definitions(KosmosRenderer
    PUBLIC GLFW_INCLUDE_NONE GLM_FORCE_RADIANS GLM_FORCE_DEPTH_ZERO_TO_ONE
    PRIVATE KOSMOS_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders")
target_link_libraries(KosmosRenderer PUBLIC Vulkan::Vulkan glm::glm
    PRIVATE basisu::basisu_encoder draco::draco glfw mikktspace::mikktspace WebP::webpdecoder)

add_executable(KosmosEditor
    src/Editor/main.cpp src/Editor/Editor.cpp src/Editor/Editor.h
    src/Editor/Camera.cpp src/Editor/Camera.h
    src/Editor/TransformGizmo.cpp src/Editor/TransformGizmo.h src/Editor/Viewport.h)
set_target_properties(KosmosEditor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
target_link_libraries(KosmosEditor PRIVATE KosmosRenderer glfw imgui::imgui)
if(WIN32)
    target_link_libraries(KosmosEditor PRIVATE comdlg32)
endif()
foreach(TARGET IN ITEMS KosmosRenderer KosmosEditor)
    set_property(TARGET ${TARGET} PROPERTY CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(${TARGET} PRIVATE /W4 /permissive- /utf-8)
        set_property(TARGET ${TARGET} PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endforeach()

# Compile shaders offline; shader errors fail the build.
foreach(SHADER IN ITEMS mesh selection shadow sky bloom postprocess irradiance prefilter brdf_lut)
    set(SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}.slang")
    if(SHADER STREQUAL "selection")
        set(SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh.slang")
        set(STAGES fragment)
    elseif(SHADER STREQUAL "mesh" OR SHADER STREQUAL "shadow" OR SHADER STREQUAL "sky" OR
       SHADER STREQUAL "bloom" OR SHADER STREQUAL "postprocess")
        set(STAGES vertex fragment)
    else()
        set(STAGES compute)
    endif()
    foreach(STAGE IN LISTS STAGES)
        set(ENTRY "${STAGE}Main")
        if(SHADER STREQUAL "selection")
            set(ENTRY selectionMain)
        endif()
        set(OUTPUT "${CMAKE_BINARY_DIR}/shaders/${SHADER}.${STAGE}.spv")
        add_custom_command(
            OUTPUT "${OUTPUT}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/shaders"
            COMMAND "${SLANGC_EXECUTABLE}" "${SHADER_SOURCE}"
                -entry "${ENTRY}" -stage "${STAGE}"
                -target spirv -profile spirv_1_6 -matrix-layout-column-major -fvk-use-entrypoint-name
                -o "${OUTPUT}"
            DEPENDS "${SHADER_SOURCE}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene.slang"
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/environment.slang"
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/brdf.slang"
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/material.slang"
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/skinning.slang"
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/morph.slang"
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/fullscreen.slang" "${SLANGC_EXECUTABLE}"
            COMMENT "Compiling Slang ${SHADER} ${STAGE} shader"
            VERBATIM)
        list(APPEND SHADER_OUTPUTS "${OUTPUT}")
    endforeach()
endforeach()
add_custom_target(KosmosShaders DEPENDS ${SHADER_OUTPUTS})
add_dependencies(KosmosRenderer KosmosShaders)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT KosmosEditor)
