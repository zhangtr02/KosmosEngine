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
    struct Buffer
    {
        VkBuffer handle{};
        VkDeviceMemory memory{};
    };
    struct Image
    {
        VkImage handle{};
        VkDeviceMemory memory{};
        VkImageView view{};
    };

    void createInstance();
    void createDevice();
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties);
    Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                        VkMemoryPropertyFlags properties);
    Buffer uploadBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage);
    void destroyBuffer(Buffer& buffer);
    void beginUpload();
    void endUpload();
    Image createImage(VkExtent2D extent, VkFormat format, VkImageUsageFlags usage,
                      VkImageAspectFlags aspect);
    void destroyImage(Image& image);
    void createTexture();
    void createDescriptors();
    void createPipeline();
    void createDepthResources();
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
    VkFormat depthFormat_{};
    Image depth_;
    Image texture_;
    VkSampler sampler_{};
    VkDescriptorSetLayout descriptorLayout_{};
    VkDescriptorPool descriptorPool_{};
    VkDescriptorSet descriptorSet_{};
    VkPipelineLayout pipelineLayout_{};
    VkPipeline pipeline_{};
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
    // Presentation semaphores belong to images, not frames in flight.
    std::vector<VkSemaphore> rendered_;
    VkCommandPool commandPool_{};
    VkCommandBuffer commandBuffer_{};
    Buffer vertexBuffer_;
    Buffer indexBuffer_;
    VkSemaphore acquired_{};
    VkFence frameFence_{};
    bool resized_ = false;
};
