#include "Editor.h"
#include "VulkanRenderer.h"

#include <GLFW/glfw3.h>
#include <glm/vec2.hpp>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <iostream>

int main()
{
    glfwSetErrorCallback(
        [](int code, const char* message)
        {
            std::cerr << "[GLFW " << code << "] " << message << '\n';
        });

    if (!glfwInit())
        return EXIT_FAILURE;

    if (!glfwVulkanSupported())
    {
        std::cerr << "Vulkan loader or driver is unavailable.\n";
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    const glm::ivec2 windowSize{1280, 720};
    GLFWwindow* window =
        glfwCreateWindow(windowSize.x, windowSize.y, "Kosmos Editor", nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        return EXIT_FAILURE;
    }
    glfwSetWindowSizeLimits(window, 680, 420, GLFW_DONT_CARE, GLFW_DONT_CARE);

    int result = EXIT_SUCCESS;
    try
    {
        VulkanRenderer renderer(window);
        Editor editor(window, renderer);
        double previousTime = glfwGetTime();
        while (!glfwWindowShouldClose(window))
        {
            glfwPollEvents();
            const double time = glfwGetTime();
            // Limit movement after a pause, such as restoring a minimized window.
            const float deltaTime = static_cast<float>(std::min(time - previousTime, 0.1));
            previousTime = time;
            editor.frame(deltaTime);
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        result = EXIT_FAILURE;
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
