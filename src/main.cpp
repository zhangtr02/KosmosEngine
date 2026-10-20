#include "VulkanRenderer.h"

#include <GLFW/glfw3.h>
#include <glm/vec2.hpp>

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
    const glm::ivec2 windowSize{1280, 720};
    GLFWwindow* window =
        glfwCreateWindow(windowSize.x, windowSize.y, "KosmosEngine", nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        return EXIT_FAILURE;
    }

    int result = EXIT_SUCCESS;
    try
    {
        VulkanRenderer renderer(window);
        while (!glfwWindowShouldClose(window))
        {
            glfwPollEvents();
            if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            if (!glfwWindowShouldClose(window))
                renderer.draw();
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
