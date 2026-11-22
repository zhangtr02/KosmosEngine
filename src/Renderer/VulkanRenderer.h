#pragma once

#include "Model.h"
#include <array>
#include <filesystem>
#include <functional>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <span>
#include <vector>
#include <vulkan/vulkan.h>

struct GLFWwindow;

struct ModelHandle
{
    size_t index;
    ModelBounds bounds;
    glm::vec3 center;
    float radius;
};

struct ModelInstance
{
    ModelHandle model;
    glm::mat4 transform{1.0f};
    bool selected = false;
};

struct OverlayTarget
{
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    uint32_t queueFamily;
    VkQueue queue;
    VkFormat format;
    uint32_t imageCount;
};

struct LocalLight
{
    bool enabled = false;
    bool spot = false;
    glm::vec3 position{1.5f, 1.5f, 1.5f};
    glm::vec3 color{1.0f, 0.7f, 0.4f}; // Linear RGB.
    float intensity = 8.0f;
    float range = 5.0f;
    float yaw = -135.0f; // Degrees; yaw 0 points along +X, pitch 0 is horizontal.
    float pitch = -35.0f;
    float innerAngle = 20.0f; // Cone half-angles in degrees.
    float outerAngle = 30.0f;
};

struct RenderSettings
{
    float exposure = 1.0f;
    float bloomStrength = 0.05f;
    float bloomThreshold = 1.0f;
    float diffuseIblStrength = 1.0f;
    float specularIblStrength = 1.0f;
    float shadowBias = 0.001f;
    float emissiveStrength = 1.0f; // Global multiplier on the authored material emission.
    float directionalIntensity = 3.0f;
    std::array<LocalLight, 4> lights{{
        {.enabled = true},
        {.position = {-1.5f, 1.5f, 1.5f}, .color = {0.3f, 0.5f, 1.0f}, .yaw = -45.0f},
        {.position = {1.5f, 1.5f, -1.5f}, .color = {0.4f, 1.0f, 0.5f}, .yaw = 135.0f},
        {.position = {-1.5f, 1.5f, -1.5f}, .color = {1.0f, 0.3f, 0.4f}, .yaw = 45.0f},
    }};
    int shadowLight = 0;
    float localShadowBias = 0.01f;  // World-space distance.
    float localFilterRadius = 1.0f; // Approximate shadow-map texels.
    bool localShadows = true;
    bool localPcf = true;
    bool bloom = true;
    bool diffuseIbl = true;
    bool specularIbl = true;
    bool environmentBackground = false;
    bool ambientOcclusion = true;
    bool shadows = true;
    bool shadowPcf = true;
    bool msaa = true;
    bool emissive = true;
};

class VulkanRenderer
{
  public:
    static constexpr float verticalFovDegrees = 45.0f;

    explicit VulkanRenderer(GLFWwindow* window);
    ~VulkanRenderer();
    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    RenderSettings settings;
    OverlayTarget overlayTarget() const;
    ModelHandle uploadModel(const std::filesystem::path& path);
    void draw(std::span<const ModelInstance> scene, const glm::mat4& view,
              const glm::vec3& cameraPosition, float nearPlane, VkRect2D viewport,
              const std::function<void(VkCommandBuffer)>& overlay = {});

  private:
    enum class PipelineType
    {
        Mesh,
        Transparent,
        Selection,
        Shadow,
        Sky,
        Bloom,
        Postprocess
    };
    enum class ShadowType
    {
        Directional,
        Point,
        Spot
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
    struct ModelResources
    {
        Buffer vertices;
        Buffer indices;
        std::vector<Image> textures;
        std::vector<Material> materials;
        std::vector<Draw> draws;
        VkDescriptorPool pool{};
        std::vector<VkDescriptorSet> sets;
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
                      VkImageAspectFlags aspect, uint32_t mipLevels = 1,
                      VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT, bool cube = false);
    void destroyImage(Image& image);
    Image createTexture(const TextureSource& source);
    Image uploadImage(const void* pixels, VkExtent2D extent, VkFormat format, VkDeviceSize bytes,
                      uint32_t mipLevels = 1);
    void createEnvironment();
    Image precomputeEnvironment(const char* shaderFilename, VkExtent2D extent,
                                uint32_t mipLevels = 1);
    void generateMipmaps(const Image& image, VkExtent2D extent, uint32_t mipLevels);
    void createDescriptors();
    void createModelDescriptors(ModelResources& model);
    void destroyModel(ModelResources& model);
    void createShadowResources();
    int shadowLightIndex() const;
    bool usesSpotShadowMap() const;
    void createPipeline(PipelineType type);
    void createRenderTargets();
    void destroyRenderTargets();
    void createSwapchain();
    void destroySwapchain();
    void destroy();
    void recordGeometry(std::span<const ModelInstance> scene, bool transparent = false,
                        const glm::mat4& view = glm::mat4(1.0f), int shadowView = -1);
    void recordShadow(std::span<const ModelInstance> scene, ShadowType type);
    void recordSelection(const ModelInstance* selected);
    void recordBloom();
    void record(uint32_t imageIndex, std::span<const ModelInstance> scene, const glm::mat4& view,
                VkRect2D viewport, const std::function<void(VkCommandBuffer)>& overlay);

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
    VkFormat selectionFormat_{};
    VkSampleCountFlagBits msaaSamples_ = VK_SAMPLE_COUNT_1_BIT;
    VkSampleCountFlagBits sceneSamples_ = VK_SAMPLE_COUNT_1_BIT;
    Image depth_;
    Image shadowMap_;
    Image pointShadowMap_;
    Image spotShadowMap_;
    std::array<VkImageView, 6> pointShadowFaces_{};
    VkSampler shadowSampler_{};
    Image hdrColor_;
    Image msaaColor_;
    Image selectionMask_;
    Image msaaSelectionMask_;
    VkExtent2D bloomExtent_{};
    std::array<Image, 2> bloomImages_{};
    std::vector<ModelResources> models_;
    Image environment_;
    Image irradiance_;
    Image prefilteredEnvironment_;
    Image brdfLut_;
    VkSampler environmentSampler_{};
    VkSampler clampSampler_{};
    VkSampler sampler_{};
    VkDescriptorSetLayout descriptorLayout_{};
    VkDescriptorPool descriptorPool_{};
    VkDescriptorSet skySet_{};
    VkPipelineLayout pipelineLayout_{};
    VkPipeline pipeline_{};
    VkPipeline transparentPipeline_{};
    VkPipeline selectionPipeline_{};
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
    Buffer sceneBuffer_;
    void* sceneMapped_{};
    VkSemaphore acquired_{};
    VkFence frameFence_{};
    bool resized_ = false;
};
