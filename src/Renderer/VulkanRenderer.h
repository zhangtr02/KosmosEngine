#pragma once

#include "Model.h"
#include <array>
#include <functional>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <utility>
#include <vector>
#include <vulkan/vulkan.h>

struct GLFWwindow;

struct ModelHandle
{
    size_t index;
    ModelBounds bounds;
    glm::vec3 center;
    float radius;
    std::shared_ptr<const ModelScene> scene;
};

struct ModelInstance
{
    ModelHandle model;
    glm::mat4 transform{1.0f};
    bool selected = false;
    const ModelPose* pose = nullptr;
    uint32_t materialVariant = defaultMaterialVariant;

    glm::mat4 nodeTransform(uint32_t node) const
    {
        return transform * (pose ? pose->world[node] : model.scene->restWorld[node]);
    }

    bool nodeVisible(uint32_t node) const
    {
        return pose ? pose->visibility[node] != 0 : model.scene->restVisibility[node] != 0;
    }
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
    LocalLight previewLight;
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
    ModelHandle uploadModel(Model&& model, const ImportProgress& progress = {});
    std::unique_lock<std::mutex> lockQueues() { return std::unique_lock(queueMutex_); }
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
    enum class GeometryPass
    {
        Opaque,
        Transmission,
        Blend,
        Selection,
        Shadow
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
        VkFormat format{};
    };
    struct ModelResources
    {
        Buffer vertices;
        Buffer indices;
        Buffer geometryData;
        Image environment;
        Image irradiance;
        std::vector<Image> textures;
        std::vector<std::pair<TextureSampler, VkSampler>> samplers;
        std::vector<Material> materials;
        std::vector<Draw> draws;
        VkDescriptorPool pool{};
        std::vector<VkDescriptorSet> sets;
    };
    struct UploadCommands
    {
        VkDevice device;
        VkCommandPool pool{};
        VkCommandBuffer command{};
        VkFence fence{};
        ~UploadCommands();
    };
    struct RenderInstance
    {
        const ModelInstance* source;
        uint32_t jointOffset;
        uint32_t morphOffset;
        uint32_t materialOffset;
        std::span<const glm::mat4> joints;
        std::span<const float> weights;
        std::span<const MaterialValues> materials;
    };
    struct ShadowCaster
    {
        size_t model;
        glm::mat4 transform;
        uint32_t variant;
        bool operator==(const ShadowCaster&) const = default;
    };
    struct LocalShadowKey
    {
        ShadowType type;
        glm::vec4 positionRange;
        glm::mat4 spotProjection;
        bool operator==(const LocalShadowKey&) const = default;
    };
    struct ShadowUpdates
    {
        const glm::mat4* sun{};
        ShadowType localType{};
        std::span<const glm::mat4> local;
    };

    void createInstance();
    void createDevice();
    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties);
    Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                        VkMemoryPropertyFlags properties);
    Buffer uploadBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage,
                        const UploadCommands& upload);
    void destroyBuffer(Buffer& buffer);
    void beginUpload(VkCommandBuffer command);
    void endUpload(VkCommandBuffer command, VkFence fence = VK_NULL_HANDLE);
    Image createImage(VkExtent2D extent, VkFormat format, VkImageUsageFlags usage,
                      VkImageAspectFlags aspect, uint32_t mipLevels = 1,
                      VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT, bool cube = false);
    void destroyImage(Image& image);
    Image createTexture(const TextureSource& source, const UploadCommands& upload);
    Image uploadImage(const void* pixels, VkExtent2D extent, VkFormat format, VkDeviceSize bytes,
                      uint32_t mipLevels, VkCommandBuffer command, VkFence fence = VK_NULL_HANDLE,
                      std::span<const size_t> mipOffsets = {});
    void createEnvironment();
    Image precomputeEnvironment(const char* shaderFilename, VkExtent2D extent,
                                uint32_t mipLevels = 1, bool sheen = false);
    void generateMipmaps(const Image& image, VkExtent2D extent, uint32_t copiedLevels,
                         uint32_t mipLevels, VkCommandBuffer command);
    void createDescriptors();
    void writeSampledImage(VkDescriptorSet set, uint32_t binding, VkImageView view,
                           VkSampler sampler);
    void createModelDescriptors(ModelResources& model);
    void destroyModel(ModelResources& model);
    void createShadowResources();
    void updateShadowCasters(std::span<const ModelInstance> scene);
    void createPipeline(PipelineType type);
    using TopologyPipelines = std::array<VkPipeline, static_cast<size_t>(PrimitiveTopology::Count)>;
    void destroyPipelines(TopologyPipelines& pipelines);
    void createRenderTargets(VkExtent2D size, VkSampleCountFlagBits samples);
    void preparePostprocess(bool bloom, bool selection);
    void destroyRenderTargets();
    void createSwapchain();
    void destroySwapchain();
    void destroy();
    void* mapPoseBuffer(Buffer& buffer, VkDeviceSize& capacity, VkDeviceSize bytes,
                        uint32_t binding, bool hasData);
    void uploadPose(std::span<const RenderInstance> instances, size_t jointCount, size_t morphCount,
                    size_t materialCount);
    void recordGeometry(std::span<const RenderInstance> scene, GeometryPass pass,
                        const glm::mat4& clip, int shadowView = -1,
                        const glm::mat4& view = glm::mat4(1.0f));
    void recordShadow(std::span<const RenderInstance> scene, ShadowType type,
                      std::span<const glm::mat4> projections);
    void recordSelection(const RenderInstance& selected, const glm::mat4& clip);
    void recordTransmissionBackground();
    void recordBloom();
    void record(uint32_t imageIndex, std::span<const RenderInstance> scene, const glm::mat4& view,
                const glm::mat4& clip, VkRect2D viewport, const RenderInstance* selected,
                bool bloom, const ShadowUpdates& shadows,
                const std::function<void(VkCommandBuffer)>& overlay);

    GLFWwindow* window_;
    VkInstance instance_{};
    VkDebugUtilsMessengerEXT messenger_{};
    VkSurfaceKHR surface_{};
    VkPhysicalDevice physicalDevice_{};
    bool bcTextures_{};
    VkDevice device_{};
    uint32_t graphicsFamily_{};
    uint32_t presentFamily_{};
    VkQueue graphicsQueue_{};
    VkQueue presentQueue_{};
    std::mutex queueMutex_;
    VkSwapchainKHR swapchain_{};
    VkFormat format_{};
    VkExtent2D extent_{};
    VkExtent2D sceneExtent_{};
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
    std::vector<ShadowCaster> shadowCasters_;
    std::optional<glm::mat4> cachedSunShadow_;
    std::optional<LocalShadowKey> cachedLocalShadow_;
    Image hdrColor_;
    Image transmissionBackground_;
    Image msaaColor_;
    Image selectionMask_;
    Image msaaSelectionMask_;
    VkExtent2D bloomExtent_{};
    std::array<Image, 2> bloomImages_{};
    std::vector<ModelResources> models_;
    std::mutex modelsMutex_;
    Image environment_;
    Image irradiance_;
    Image prefilteredEnvironment_;
    Image prefilteredSheen_;
    Image brdfLut_;
    VkSampler environmentSampler_{};
    VkSampler clampSampler_{};
    VkSampler transmissionSampler_{};
    VkSampler sampler_{};
    VkDescriptorSetLayout descriptorLayout_{};
    VkDescriptorSetLayout jointLayout_{};
    VkDescriptorPool descriptorPool_{};
    VkDescriptorSet skySet_{};
    VkDescriptorSet jointSet_{};
    std::array<VkDescriptorSet, 2> transmissionSets_{};
    VkPipelineLayout pipelineLayout_{};
    TopologyPipelines meshPipelines_{};
    TopologyPipelines transparentPipelines_{};
    TopologyPipelines selectionPipelines_{};
    TopologyPipelines shadowPipelines_{};
    VkPipeline skyPipeline_{};
    VkDescriptorSetLayout bloomDescriptorLayout_{};
    std::array<VkDescriptorSet, 3> bloomSets_{};
    VkPipelineLayout bloomPipelineLayout_{};
    VkPipeline bloomPipeline_{};
    VkDescriptorSetLayout postprocessDescriptorLayout_{};
    VkDescriptorSet postprocessSet_{};
    VkImageView boundBloomView_{};
    VkImageView boundSelectionView_{};
    VkPipelineLayout postprocessPipelineLayout_{};
    VkPipeline postprocessPipeline_{};
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
    // Presentation semaphores belong to images, not frames in flight.
    std::vector<VkSemaphore> rendered_;
    VkCommandPool commandPool_{};
    VkCommandBuffer commandBuffer_{};
    Buffer sceneBuffer_;
    Buffer jointBuffer_;
    VkDeviceSize jointBufferSize_{};
    Buffer emptyGeometryBuffer_;
    Buffer morphWeightBuffer_;
    VkDeviceSize morphWeightBufferSize_{};
    Buffer materialBuffer_;
    VkDeviceSize materialBufferSize_{};
    void* sceneMapped_{};
    VkSemaphore acquired_{};
    VkFence frameFence_{};
    bool resized_ = false;
};
