set(BUILD_SHARED_LIBS OFF)

# =========================
# GLFW
# =========================

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory(glfw)

# =========================
# GLM
# =========================

set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory(glm)

# =========================
# stb_image
# =========================

add_library(stb_image INTERFACE)
target_include_directories(stb_image INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb)

# =========================
# Dear ImGui
# =========================

add_library(imgui STATIC
    imgui/imgui.cpp
    imgui/imgui_demo.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/backends/imgui_impl_glfw.cpp
    imgui/backends/imgui_impl_vulkan.cpp
)

target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends
)

target_link_libraries(imgui PUBLIC Vulkan::Vulkan glfw)