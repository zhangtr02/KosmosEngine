cmake_minimum_required(VERSION 4.2)
project(KosmosEngine LANGUAGES CXX)

find_package(Vulkan 1.3 REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_program(SLANGC_EXECUTABLE NAMES slangc
    HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)

add_executable(KosmosEngine src/main.cpp src/VulkanRenderer.cpp src/VulkanRenderer.h)
target_compile_features(KosmosEngine PRIVATE cxx_std_20)
set_target_properties(KosmosEngine PROPERTIES
    CXX_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
target_compile_definitions(KosmosEngine PRIVATE
    GLFW_INCLUDE_NONE GLM_FORCE_RADIANS GLM_FORCE_DEPTH_ZERO_TO_ONE
    KOSMOS_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders")
target_link_libraries(KosmosEngine PRIVATE Vulkan::Vulkan glfw glm::glm)
if(MSVC)
    target_compile_options(KosmosEngine PRIVATE /W4 /permissive- /utf-8)
    set_property(TARGET KosmosEngine PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Compile both entry points offline; shader errors fail the build.
set(SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh.slang")
foreach(STAGE IN ITEMS vertex fragment)
    set(OUTPUT "${CMAKE_BINARY_DIR}/shaders/mesh.${STAGE}.spv")
    add_custom_command(
        OUTPUT "${OUTPUT}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/shaders"
        COMMAND "${SLANGC_EXECUTABLE}" "${SHADER_SOURCE}"
            -entry "${STAGE}Main" -stage "${STAGE}"
            -target spirv -profile spirv_1_6 -matrix-layout-column-major -fvk-use-entrypoint-name
            -o "${OUTPUT}"
        DEPENDS "${SHADER_SOURCE}" "${SLANGC_EXECUTABLE}"
        COMMENT "Compiling Slang ${STAGE} shader"
        VERBATIM)
    list(APPEND SHADER_OUTPUTS "${OUTPUT}")
endforeach()
add_custom_target(KosmosShaders DEPENDS ${SHADER_OUTPUTS})
add_dependencies(KosmosEngine KosmosShaders)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT KosmosEngine)
