cmake_minimum_required(VERSION 3.25)

project(KosmosEngine
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Bin)

find_package(Vulkan REQUIRED)

add_subdirectory(ThirdParty)
add_subdirectory(Engine)
add_subdirectory(Sandbox)

find_program(DXC_EXECUTABLE
    NAMES dxc.exe
    HINTS "$ENV{VULKAN_SDK}/Bin"
    REQUIRED
)

set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Shaders")
set(SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/Shaders")

set(FORWARD_LIT_VERT_SHADER "${SHADER_SOURCE_DIR}/ForwardLit.vert.hlsl")
set(FORWARD_LIT_FRAG_SHADER "${SHADER_SOURCE_DIR}/ForwardLit.frag.hlsl")
set(FULLSCREEN_VERT_SHADER "${SHADER_SOURCE_DIR}/Fullscreen.vert.hlsl")
set(FULLSCREEN_FRAG_SHADER "${SHADER_SOURCE_DIR}/Fullscreen.frag.hlsl")

set(FORWARD_LIT_VERT_SPV "${SHADER_OUTPUT_DIR}/ForwardLit.vert.spv")
set(FORWARD_LIT_FRAG_SPV "${SHADER_OUTPUT_DIR}/ForwardLit.frag.spv")
set(FULLSCREEN_VERT_SPV "${SHADER_OUTPUT_DIR}/Fullscreen.vert.spv")
set(FULLSCREEN_FRAG_SPV "${SHADER_OUTPUT_DIR}/Fullscreen.frag.spv")

add_custom_command(
    OUTPUT ${FORWARD_LIT_VERT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
    COMMAND ${DXC_EXECUTABLE} -spirv -T vs_6_0 -E main -Fo ${FORWARD_LIT_VERT_SPV} ${FORWARD_LIT_VERT_SHADER}
    DEPENDS ${FORWARD_LIT_VERT_SHADER}
    VERBATIM
)

add_custom_command(
    OUTPUT ${FORWARD_LIT_FRAG_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
    COMMAND ${DXC_EXECUTABLE} -spirv -T ps_6_0 -E main -Fo ${FORWARD_LIT_FRAG_SPV} ${FORWARD_LIT_FRAG_SHADER}
    DEPENDS ${FORWARD_LIT_FRAG_SHADER}
    VERBATIM
)

add_custom_command(
    OUTPUT ${FULLSCREEN_VERT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
    COMMAND ${DXC_EXECUTABLE} -spirv -T vs_6_0 -E main -Fo ${FULLSCREEN_VERT_SPV} ${FULLSCREEN_VERT_SHADER}
    DEPENDS ${FULLSCREEN_VERT_SHADER}
    VERBATIM
)

add_custom_command(
    OUTPUT ${FULLSCREEN_FRAG_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUTPUT_DIR}
    COMMAND ${DXC_EXECUTABLE} -spirv -T ps_6_0 -E main -Fo ${FULLSCREEN_FRAG_SPV} ${FULLSCREEN_FRAG_SHADER}
    DEPENDS ${FULLSCREEN_FRAG_SHADER}
    VERBATIM
)

add_custom_target(KosmosShaders
    DEPENDS
        ${FORWARD_LIT_VERT_SPV}
        ${FORWARD_LIT_FRAG_SPV}
        ${FULLSCREEN_VERT_SPV}
        ${FULLSCREEN_FRAG_SPV}
)

add_dependencies(Sandbox KosmosShaders)

target_compile_definitions(KosmosEngine
    PRIVATE
        KOSMOS_SHADER_DIR="${SHADER_OUTPUT_DIR}"
         KOSMOS_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Assets"
)