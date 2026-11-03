#pragma once

#include "Model.h"
#include <array>
#include <filesystem>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <vector>
#include <vulkan/vulkan.h>

struct GLFWwindow;
struct ImGuiContext;

struct RenderSettings
{
    float exposure = 1.0f;
    float bloomStrength = 0.05f;
    float bloomThreshold = 1.0f;
    float diffuseIblStrength = 1.0f;
    float specularIblStrength = 1.0f;
    float shadowBias = 0.001f;
    float emissiveStrength = 1.0f; // Global multiplier on the authored material emission.
    bool bloom = true;
    bool diffuseIbl = true;
    bool specularIbl = true;
    bool ambientOcclusion = true;
    bool rotateModel = true;
    bool shadows = true;
    bool shadowPcf = true;
    bool msaa = true;
    bool emissive = true;
};

class VulkanRenderer
{
  public:
    explicit VulkanRenderer(GLFWwindow* window);
    ~VulkanRenderer();
    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    RenderSettings settings;
    void updateUI();
    void draw(const glm::mat4& view, const glm::vec3& cameraPosition, float deltaTime);

  private:
    enum class PipelineType
    {
        Mesh,
        Shadow,
        Sky,
        Bloom,
        Postprocess
    };
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
    void createUI();
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties);
    Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                        VkMemoryPropertyFlags properties);
    Buffer uploadBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage);
    void destroyBuffer(Buffer& buffer);
    void beginUpload();
    void endUpload();
    Image createImage(VkExtent2D extent, VkFormat format, VkImageUsageFlags usage,
                      VkImageAspectFlags aspect, uint32_t mipLevels = 1,
                      VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
    void destroyImage(Image& image);
    Image createTexture(const TextureSource& source);
    Image uploadImage(const void* pixels, VkExtent2D extent, VkFormat format, VkDeviceSize bytes,
                      uint32_t mipLevels = 1);
    void createEnvironment();
    Image precomputeEnvironment(const char* shaderFilename, VkExtent2D extent,
                                uint32_t mipLevels = 1);
    void generateMipmaps(const Image& image, VkExtent2D extent, uint32_t mipLevels);
    void createDescriptors();
    void createShadowResources();
    void createPipeline(PipelineType type);
    void createRenderTargets();
    void destroyRenderTargets();
    void createSwapchain();
    void destroySwapchain();
    void destroy();
    void recordGeometry();
    void recordShadow();
    void recordBloom();
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
    VkSampleCountFlagBits msaaSamples_ = VK_SAMPLE_COUNT_1_BIT;
    VkSampleCountFlagBits sceneSamples_ = VK_SAMPLE_COUNT_1_BIT;
    Image depth_;
    Image shadowMap_;
    VkSampler shadowSampler_{};
    Image hdrColor_;
    Image msaaColor_;
    VkExtent2D bloomExtent_{};
    std::array<Image, 2> bloomImages_{};
    std::vector<Image> textures_;
    Image environment_;
    Image irradiance_;
    Image prefilteredEnvironment_;
    Image brdfLut_;
    VkSampler environmentSampler_{};
    VkSampler clampSampler_{};
    VkSampler sampler_{};
    VkDescriptorSetLayout descriptorLayout_{};
    VkDescriptorPool descriptorPool_{};
    std::vector<VkDescriptorSet> descriptorSets_;
    VkPipelineLayout pipelineLayout_{};
    VkPipeline pipeline_{};
    VkPipeline shadowPipeline_{};
    VkPipeline skyPipeline_{};
    VkDescriptorSetLayout bloomDescriptorLayout_{};
    std::array<VkDescriptorSet, 3> bloomSets_{};
    VkPipelineLayout bloomPipelineLayout_{};
    VkPipeline bloomPipeline_{};
    VkDescriptorSetLayout postprocessDescriptorLayout_{};
    VkDescriptorSet postprocessSet_{};
    VkPipelineLayout postprocessPipelineLayout_{};
    VkPipeline postprocessPipeline_{};
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
    Draw groundDraw_;
    VkSemaphore acquired_{};
    VkFence frameFence_{};
    bool resized_ = false;
    ImGuiContext* uiContext_{};
    float modelAngle_ = 0.0f;
};
