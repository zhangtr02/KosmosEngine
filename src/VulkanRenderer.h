#pragma once

#include "Model.h"
#include <filesystem>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
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

    void draw(const glm::mat4& view, const glm::vec3& cameraPosition);

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
                      VkImageAspectFlags aspect, uint32_t mipLevels = 1);
    void destroyImage(Image& image);
    Image createTexture(const std::filesystem::path& path);
    void generateMipmaps(const Image& image, VkExtent2D extent, uint32_t mipLevels);
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
    std::vector<Image> textures_;
    VkSampler sampler_{};
    VkDescriptorSetLayout descriptorLayout_{};
    VkDescriptorPool descriptorPool_{};
    std::vector<VkDescriptorSet> descriptorSets_;
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
    Buffer sceneBuffer_;
    void* sceneMapped_{};
    std::vector<Material> materials_;
    std::vector<Draw> draws_;
    VkSemaphore acquired_{};
    VkFence frameFence_{};
    bool resized_ = false;
};
