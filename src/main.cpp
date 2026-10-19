#include <GLFW/glfw3.h>
#include <glm/vec2.hpp>

#include <cstdlib>
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

    std::cout << "KosmosEngine: window ready, Vulkan available. ESC to exit." << std::endl;
    while (!glfwWindowShouldClose(window))
    {
        // No rendering yet; sleep until a window/input event arrives.
        glfwWaitEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
