#pragma once

#include <vector>
#include <vulkan/vulkan.h>

struct GLFWwindow;

class VulkanRenderer
{
  public:
    explicit VulkanRenderer(GLFWwindow* window);
    ~VulkanRenderer();
    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    void draw();

  private:
    void createInstance();
    void createDevice();
    void createSwapchain();
    void destroySwapchain();
    void destroy();
    void record(uint32_t imageIndex);

    GLFWwindow* window_;
    VkInstance instance_{};
    VkDebugUtilsMessengerEXT messenger_{};
    VkSurfaceKHR surface_{};
    VkPhysicalDevice physicalDevice_{};
    VkDevice device_{};
    uint32_t graphicsFamily_{};
    uint32_t presentFamily_{};
    VkQueue graphicsQueue_{};
    VkQueue presentQueue_{};
    VkSwapchainKHR swapchain_{};
    VkFormat format_{};
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
    // Presentation semaphores belong to images, not frames in flight.
    std::vector<VkSemaphore> rendered_;
    VkCommandPool commandPool_{};
    VkCommandBuffer commandBuffer_{};
    VkSemaphore acquired_{};
    VkFence frameFence_{};
    bool resized_ = false;
};
