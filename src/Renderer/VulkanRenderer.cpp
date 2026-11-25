#include "VulkanRenderer.h"

#include "Texture.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
constexpr VkFormat hdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkImageUsageFlags msaaColorUsage =
    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
constexpr VkExtent2D shadowExtent{2048, 2048};
constexpr VkExtent2D pointShadowExtent{512, 512};
constexpr float maxSpotShadowAngle = 80.0f;
constexpr size_t maxPunctualLights = 8;
constexpr uint32_t blendFlag = 1, unlitFlag = 2, flatMorphNormalsFlag = 4, deriveTangentFlag = 8,
                   animatedUvFlag = 16, materialIndexShift = 5;
constexpr uint32_t advancedTextureBase = 14;
constexpr uint32_t sheenEnvironmentBinding =
    advancedTextureBase + MaterialTextureCount - BaseTextureCount;
constexpr uint32_t materialBindingCount = sheenEnvironmentBinding + 1;
constexpr uint32_t materialSamplerCount = materialBindingCount - 2; // Scene and geometry buffers.

struct PushConstants
{
    glm::mat4 model;
    glm::vec4 baseColorFactor;
    glm::vec4 materialParams; // Normal scale, metallic factor, roughness factor, AO strength.
    glm::vec3 emissiveFactor; // Linear RGB, including material strength and the UI multiplier.
    uint32_t jointOffset;
    float alphaCutoff;
    int32_t shadowView; // -2 Spot, -1 Sun, 0..5 Cube.
    uint32_t flags;     // Surface flags in the low bits, material buffer index above them.
    uint32_t morphWeightOffset;
};
static_assert(sizeof(PushConstants) == 128 && offsetof(PushConstants, baseColorFactor) == 64 &&
              offsetof(PushConstants, materialParams) == 80 &&
              offsetof(PushConstants, emissiveFactor) == 96 &&
              offsetof(PushConstants, jointOffset) == 108 &&
              offsetof(PushConstants, alphaCutoff) == 112 &&
              offsetof(PushConstants, morphWeightOffset) == 124);
static_assert(sizeof(MorphDelta) == 48);

struct UvTransform
{
    glm::vec4 axes;
    glm::vec4 offset;
};
struct GpuMaterial
{
    std::array<UvTransform, MaterialTextureCount> uv;
    AdvancedMaterial advanced;
};
static_assert(sizeof(UvTransform) == 32 && sizeof(AdvancedMaterial) == 160 &&
              sizeof(GpuMaterial) == 800);

struct LightData
{
    glm::vec4 positionRange;
    glm::vec4 colorIntensity{0.0f};
    glm::vec4 directionType;
    glm::vec4 cone; // Inner/outer cosine, minimum squared distance, unused W.
};
static_assert(sizeof(LightData) == 64 && offsetof(LightData, colorIntensity) == 16 &&
              offsetof(LightData, directionType) == 32 && offsetof(LightData, cone) == 48);

struct SceneData
{
    glm::mat4 viewProjection;
    glm::vec4 cameraPosition;
    glm::vec4 lighting{}; // Diffuse IBL, exposure, specular IBL, Bloom; set from RenderSettings.
    glm::mat4 inverseSkyViewProjection;
    glm::mat4 lightViewProjection;
    glm::vec4 shadowParams; // Directional light index (-1 disables), depth bias, PCF, unused.
    std::array<LightData, maxPunctualLights> lights{};
    glm::vec4 localShadowParams;     // Light index (-1 disables), world-space bias, PCF, radius.
    glm::vec4 localShadowProjection; // 2D enabled, tangent of outer half-angle, unused ZW.
    std::array<glm::mat4, 6> pointViewProjections;
    glm::mat4 spotViewProjection;
};
static_assert(sizeof(SceneData) == 1232 && offsetof(SceneData, cameraPosition) == 64 &&
              offsetof(SceneData, lighting) == 80 &&
              offsetof(SceneData, inverseSkyViewProjection) == 96 &&
              offsetof(SceneData, lightViewProjection) == 160 &&
              offsetof(SceneData, shadowParams) == 224 && offsetof(SceneData, lights) == 240 &&
              offsetof(SceneData, localShadowParams) == 752 &&
              offsetof(SceneData, localShadowProjection) == 768 &&
              offsetof(SceneData, pointViewProjections) == 784 &&
              offsetof(SceneData, spotViewProjection) == 1168);

void check(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + ": VkResult " + std::to_string(result));
}

VkShaderModule loadShader(VkDevice device, const char* filename)
{
    const auto path = std::filesystem::path(KOSMOS_SHADER_DIR) / filename;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("Cannot open shader: " + path.string());
    const auto size = file.tellg();
    if (size <= 0 || size % sizeof(uint32_t) != 0)
        throw std::runtime_error("Invalid SPIR-V size: " + path.string());
    std::vector<uint32_t> code(static_cast<size_t>(size) / sizeof(uint32_t));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(size)))
        throw std::runtime_error("Cannot read shader: " + path.string());
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = static_cast<size_t>(size);
    info.pCode = code.data();
    VkShaderModule shader{};
    check(vkCreateShaderModule(device, &info, nullptr, &shader), "Create shader module");
    return shader;
}

#ifndef NDEBUG
VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT,
                                             VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data,
                                             void*)
{
    std::cerr << "[Vulkan] " << data->pMessage << '\n';
    return VK_FALSE;
}
#endif
} // namespace

VulkanRenderer::VulkanRenderer(GLFWwindow* window) : window_(window)
{
    try
    {
        createInstance();
        check(glfwCreateWindowSurface(instance_, window_, nullptr, &surface_), "Create surface");
        createDevice();

        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool.queueFamilyIndex = graphicsFamily_;
        check(vkCreateCommandPool(device_, &pool, nullptr, &commandPool_), "Create command pool");
        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocate.commandPool = commandPool_;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(device_, &allocate, &commandBuffer_),
              "Allocate command buffer");

        createEnvironment();
        irradiance_ = precomputeEnvironment("irradiance.compute.spv", {64, 32});
        // Stop at 16x8: even rough reflections retain directional lighting.
        prefilteredEnvironment_ = precomputeEnvironment("prefilter.compute.spv", {256, 128}, 5);
        prefilteredSheen_ = precomputeEnvironment("prefilter.compute.spv", {128, 64}, 5, true);
        brdfLut_ = precomputeEnvironment("brdf_lut.compute.spv", {256, 256});
        createShadowResources();
        createDescriptors();

        VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        check(vkCreateSemaphore(device_, &semaphore, nullptr, &acquired_),
              "Create acquire semaphore");
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        check(vkCreateFence(device_, &fence, nullptr, &frameFence_), "Create frame fence");
        createSwapchain();
    }
    catch (...)
    {
        destroy();
        throw;
    }

    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(
        window_,
        [](GLFWwindow* window, int, int)
        {
            static_cast<VulkanRenderer*>(glfwGetWindowUserPointer(window))->resized_ = true;
        });
}

VulkanRenderer::~VulkanRenderer()
{
    glfwSetFramebufferSizeCallback(window_, nullptr);
    glfwSetWindowUserPointer(window_, nullptr);
    destroy();
}

OverlayTarget VulkanRenderer::overlayTarget() const
{
    return {instance_,
            physicalDevice_,
            device_,
            graphicsFamily_,
            graphicsQueue_,
            format_,
            static_cast<uint32_t>(images_.size())};
}

VulkanRenderer::UploadCommands::~UploadCommands()
{
    vkDestroyFence(device, fence, nullptr);
    vkDestroyCommandPool(device, pool, nullptr);
}

ModelHandle VulkanRenderer::uploadModel(Model&& model, const ImportProgress& progress)
{
    if (progress)
        progress(ImportStage::UploadingGeometry);
    UploadCommands upload{device_};
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags =
        VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = graphicsFamily_;
    check(vkCreateCommandPool(device_, &pool, nullptr, &upload.pool), "Create model upload pool");
    VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool = upload.pool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(device_, &allocate, &upload.command),
          "Allocate model upload commands");
    const VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    check(vkCreateFence(device_, &fence, nullptr, &upload.fence), "Create model upload fence");

    ModelResources replacement;
    replacement.materials = std::move(model.materials);
    replacement.draws = std::move(model.draws);
    try
    {
        const size_t morphBytes = model.morphDeltas.size() * sizeof(MorphDelta);
        const size_t uvBytes = model.advancedUvs.size() * sizeof(glm::vec4);
        if (const size_t bytes = morphBytes + uvBytes; bytes != 0)
        {
            VkPhysicalDeviceProperties properties;
            vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
            if (bytes > properties.limits.maxStorageBufferRange)
                throw std::runtime_error("Geometry data exceeds the device storage buffer limit");
            if (uvBytes)
            {
                std::vector<std::byte> payload(bytes);
                if (morphBytes)
                    std::memcpy(payload.data(), model.morphDeltas.data(), morphBytes);
                std::memcpy(payload.data() + morphBytes, model.advancedUvs.data(), uvBytes);
                for (auto& vertex : model.vertices)
                    if (vertex.advancedUv)
                        vertex.advancedUv += static_cast<uint32_t>(morphBytes / sizeof(glm::vec4));
                replacement.geometryData =
                    uploadBuffer(payload.data(), bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, upload);
            }
            else
                replacement.geometryData = uploadBuffer(model.morphDeltas.data(), bytes,
                                                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, upload);
        }
        replacement.vertices =
            uploadBuffer(model.vertices.data(), model.vertices.size() * sizeof(Vertex),
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, upload);
        if (progress)
            progress(ImportStage::UploadingGeometry);
        replacement.indices =
            uploadBuffer(model.indices.data(), model.indices.size() * sizeof(uint32_t),
                         VK_BUFFER_USAGE_INDEX_BUFFER_BIT, upload);
        replacement.textures.resize(model.textures.size());
        for (size_t i = 0; i < model.textures.size(); ++i)
        {
            if (progress)
                progress(ImportStage::UploadingTextures);
            replacement.textures[i] = createTexture(model.textures[i], upload);
        }
        if (model.environment)
        {
            if (progress)
                progress(ImportStage::UploadingTextures);
            VkPhysicalDeviceProperties properties;
            vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
            const auto environment =
                decodeEnvironment(*model.environment, properties.limits.maxImageDimension2D);
            constexpr auto format = VK_FORMAT_R32G32B32A32_SFLOAT;
            replacement.environment =
                uploadImage(environment.radiance.data(), {environment.width, environment.height},
                            format, environment.radiance.size() * sizeof(glm::vec4),
                            static_cast<uint32_t>(environment.mipOffsets.size()), upload.command,
                            upload.fence, environment.mipOffsets);
            replacement.irradiance = uploadImage(environment.irradiance.data(), {64, 32}, format,
                                                 environment.irradiance.size() * sizeof(glm::vec4),
                                                 1, upload.command, upload.fence);
        }
        if (progress)
            progress(ImportStage::Finalizing);
        createModelDescriptors(replacement);
        if (progress)
            progress(ImportStage::Finalizing);
        std::lock_guard lock(modelsMutex_);
        const ModelHandle handle{models_.size(), model.bounds, model.center, model.radius,
                                 std::make_shared<ModelScene>(std::move(model.scene))};
        models_.push_back(std::move(replacement));
        return handle;
    }
    catch (...)
    {
        destroyModel(replacement);
        throw;
    }
}

void VulkanRenderer::destroyModel(ModelResources& model)
{
    vkDestroyDescriptorPool(device_, model.pool, nullptr);
    destroyImage(model.environment);
    destroyImage(model.irradiance);
    for (const auto& entry : model.samplers)
        vkDestroySampler(device_, entry.second, nullptr);
    for (auto& texture : model.textures)
        destroyImage(texture);
    destroyBuffer(model.indices);
    destroyBuffer(model.vertices);
    destroyBuffer(model.geometryData);
}

void VulkanRenderer::createInstance()
{
    uint32_t count = 0;
    const char** required = glfwGetRequiredInstanceExtensions(&count);
    if (!required)
        throw std::runtime_error("GLFW cannot find Vulkan surface extensions");
    std::vector<const char*> extensions(required, required + count);
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "KosmosEngine";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;

#ifndef NDEBUG
    const char* layer = "VK_LAYER_KHRONOS_validation";
    uint32_t layerCount = 0;
    check(vkEnumerateInstanceLayerProperties(&layerCount, nullptr), "Enumerate instance layers");
    std::vector<VkLayerProperties> layers(layerCount);
    check(vkEnumerateInstanceLayerProperties(&layerCount, layers.data()),
          "Enumerate instance layers");
    if (std::ranges::none_of(layers,
                             [layer](const auto& p)
                             {
                                 return std::strcmp(p.layerName, layer) == 0;
                             }))
        throw std::runtime_error("Install the Vulkan SDK validation layer for Debug builds");
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    VkDebugUtilsMessengerCreateInfoEXT debug{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug.pfnUserCallback = debugCallback;
    info.enabledLayerCount = 1;
    info.ppEnabledLayerNames = &layer;
    info.pNext = &debug;
#endif
    info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    check(vkCreateInstance(&info, nullptr, &instance_), "Create Vulkan 1.3 instance");
#ifndef NDEBUG
    auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
    if (!createMessenger)
        throw std::runtime_error("Debug utils extension is unavailable");
    check(createMessenger(instance_, &debug, nullptr, &messenger_), "Create debug messenger");
#endif
}

void VulkanRenderer::createDevice()
{
    uint32_t count = 0;
    check(vkEnumeratePhysicalDevices(instance_, &count, nullptr), "Enumerate GPUs");
    std::vector<VkPhysicalDevice> devices(count);
    check(vkEnumeratePhysicalDevices(instance_, &count, devices.data()), "Enumerate GPUs");
    for (auto candidate : devices)
    {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(candidate, &properties);
        VkPhysicalDeviceVulkan13Features features13{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &features13;
        vkGetPhysicalDeviceFeatures2(candidate, &features);
        if (properties.apiVersion < VK_API_VERSION_1_3 ||
            properties.limits.maxPerStageDescriptorSamplers < materialSamplerCount + 1 ||
            properties.limits.maxPerStageDescriptorSampledImages < materialSamplerCount + 1 ||
            properties.limits.maxVertexOutputComponents < 76 ||
            properties.limits.maxFragmentInputComponents < 76 || !features13.dynamicRendering ||
            !features13.synchronization2 || !features13.shaderDemoteToHelperInvocation)
            continue;

        uint32_t extensionCount = 0;
        check(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, nullptr),
              "Enumerate device extensions");
        std::vector<VkExtensionProperties> extensions(extensionCount);
        check(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount,
                                                   extensions.data()),
              "Enumerate device extensions");
        if (std::ranges::none_of(extensions,
                                 [](const auto& p)
                                 {
                                     return std::strcmp(p.extensionName,
                                                        VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
                                 }))
            continue;

        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
        uint32_t graphics = UINT32_MAX, present = UINT32_MAX;
        for (uint32_t i = 0; i < familyCount; ++i)
        {
            VkBool32 supported = VK_FALSE;
            check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface_, &supported),
                  "Query presentation support");
            constexpr VkQueueFlags required = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
            const bool graphicsCompute = (families[i].queueFlags & required) == required;
            if (graphicsCompute)
                graphics = i;
            if (supported)
                present = i;
            if (graphicsCompute && supported)
            {
                graphics = present = i;
                break;
            }
        }
        uint32_t formatCount = 0, modeCount = 0;
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface_, &formatCount, nullptr),
              "Query surface formats");
        check(vkGetPhysicalDeviceSurfacePresentModesKHR(candidate, surface_, &modeCount, nullptr),
              "Query present modes");
        if (graphics == UINT32_MAX || present == UINT32_MAX || !formatCount || !modeCount)
            continue;
        physicalDevice_ = candidate;
        graphicsFamily_ = graphics;
        presentFamily_ = present;
        std::cout << "Vulkan GPU: " << properties.deviceName << std::endl;
        break;
    }
    if (!physicalDevice_)
        throw std::runtime_error("No GPU supports Vulkan 1.3, graphics/compute, dynamic rendering, "
                                 "synchronization2, presentation and the PBR shader limits");

    const float priority = 1.0f;
    const std::array families{graphicsFamily_, presentFamily_};
    std::vector<VkDeviceQueueCreateInfo> queues;
    for (uint32_t family : families)
    {
        if (!queues.empty() && family == queues.front().queueFamilyIndex)
            continue;
        VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue.queueFamilyIndex = family;
        queue.queueCount = 1;
        queue.pQueuePriorities = &priority;
        queues.push_back(queue);
    }
    VkPhysicalDeviceVulkan13Features features{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features.dynamicRendering = VK_TRUE;
    features.synchronization2 = VK_TRUE;
    features.shaderDemoteToHelperInvocation = VK_TRUE; // Slang discard keeps helper derivatives.
    const char* extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.pNext = &features;
    info.queueCreateInfoCount = static_cast<uint32_t>(queues.size());
    info.pQueueCreateInfos = queues.data();
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = &extension;
    check(vkCreateDevice(physicalDevice_, &info, nullptr, &device_), "Create logical device");
    vkGetDeviceQueue(device_, graphicsFamily_, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, presentFamily_, 0, &presentQueue_);
}

uint32_t VulkanRenderer::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memory;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memory);
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
        if ((typeBits & (1u << i)) &&
            (memory.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    throw std::runtime_error("No compatible memory type");
}

VulkanRenderer::Buffer VulkanRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                                    VkMemoryPropertyFlags properties)
{
    Buffer buffer;
    try
    {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateBuffer(device_, &info, nullptr, &buffer.handle), "Create buffer");
        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(device_, buffer.handle, &requirements);
        VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, properties);
        check(vkAllocateMemory(device_, &allocate, nullptr, &buffer.memory),
              "Allocate buffer memory");
        check(vkBindBufferMemory(device_, buffer.handle, buffer.memory, 0), "Bind buffer memory");
    }
    catch (...)
    {
        destroyBuffer(buffer);
        throw;
    }
    return buffer;
}

VulkanRenderer::Buffer VulkanRenderer::uploadBuffer(const void* data, VkDeviceSize size,
                                                    VkBufferUsageFlags usage,
                                                    const UploadCommands& upload)
{
    Buffer staging;
    Buffer destination;
    try
    {
        staging = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void* mapped = nullptr;
        check(vkMapMemory(device_, staging.memory, 0, size, 0, &mapped), "Map staging memory");
        std::memcpy(mapped, data, static_cast<size_t>(size));
        vkUnmapMemory(device_, staging.memory);
        destination = createBuffer(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        beginUpload(upload.command);
        const VkBufferCopy region{0, 0, size};
        vkCmdCopyBuffer(upload.command, staging.handle, destination.handle, 1, &region);
        VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        if (usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
        {
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
        }
        else
        {
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
            barrier.dstAccessMask =
                VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT;
        }
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.memoryBarrierCount = 1;
        dependency.pMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(upload.command, &dependency);
        endUpload(upload.command, upload.fence);
    }
    catch (...)
    {
        destroyBuffer(staging);
        destroyBuffer(destination);
        throw;
    }
    destroyBuffer(staging);
    return destination;
}

void VulkanRenderer::destroyBuffer(Buffer& buffer)
{
    vkDestroyBuffer(device_, buffer.handle, nullptr);
    vkFreeMemory(device_, buffer.memory, nullptr);
    buffer = {};
}

void VulkanRenderer::beginUpload(VkCommandBuffer command)
{
    check(vkResetCommandBuffer(command, 0), "Reset upload command buffer");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(command, &begin), "Begin upload");
}

void VulkanRenderer::endUpload(VkCommandBuffer commands, VkFence fence)
{
    check(vkEndCommandBuffer(commands), "End upload");
    VkCommandBufferSubmitInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    command.commandBuffer = commands;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command;
    if (fence)
        check(vkResetFences(device_, 1, &fence), "Reset upload fence");
    {
        auto lock = lockQueues();
        check(vkQueueSubmit2(graphicsQueue_, 1, &submit, fence), "Submit upload");
        if (!fence)
            check(vkQueueWaitIdle(graphicsQueue_), "Wait for startup upload");
    }
    if (fence)
        check(vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX), "Wait for upload fence");
}

VulkanRenderer::Image VulkanRenderer::createImage(VkExtent2D extent, VkFormat format,
                                                  VkImageUsageFlags usage,
                                                  VkImageAspectFlags aspect, uint32_t mipLevels,
                                                  VkSampleCountFlagBits samples, bool cube)
{
    Image image;
    try
    {
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.flags = cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {extent.width, extent.height, 1};
        info.mipLevels = mipLevels;
        info.arrayLayers = cube ? 6 : 1;
        info.samples = samples;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        check(vkCreateImage(device_, &info, nullptr, &image.handle), "Create image");
        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(device_, image.handle, &requirements);
        VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex =
            findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkAllocateMemory(device_, &allocate, nullptr, &image.memory),
              "Allocate image memory");
        check(vkBindImageMemory(device_, image.handle, image.memory, 0), "Bind image memory");
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = image.handle;
        view.viewType = cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange = {aspect, 0, mipLevels, 0, info.arrayLayers};
        check(vkCreateImageView(device_, &view, nullptr, &image.view), "Create image view");
    }
    catch (...)
    {
        destroyImage(image);
        throw;
    }
    return image;
}

void VulkanRenderer::destroyImage(Image& image)
{
    vkDestroyImageView(device_, image.view, nullptr);
    vkDestroyImage(device_, image.handle, nullptr);
    vkFreeMemory(device_, image.memory, nullptr);
    image = {};
}

VulkanRenderer::Image VulkanRenderer::createTexture(const TextureSource& source,
                                                    const UploadCommands& upload)
{
    const VkFormat format = source.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    if (!source.hasImage())
    {
        const auto fallback = source.flatNormal ? std::array<uint8_t, 4>{128, 128, 255, 255}
                                                : std::array<uint8_t, 4>{255, 255, 255, 255};
        return uploadImage(fallback.data(), {1, 1}, format, fallback.size(), 1, upload.command,
                           upload.fence);
    }
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(physicalDevice_, &deviceProperties);
    const auto texture = decodeTexture(source, deviceProperties.limits.maxImageDimension2D);
    const VkExtent2D extent{texture.width, texture.height};
    const uint32_t mipLevels = std::bit_width(std::max(extent.width, extent.height));
    auto image = uploadImage(texture.pixels.get(), extent, format, texture.bytes, mipLevels,
                             upload.command, upload.fence, texture.mipOffsets);
    std::cout << "Texture: " << extent.width << 'x' << extent.height << ", " << mipLevels
              << " mip levels\n";
    return image;
}

VulkanRenderer::Image VulkanRenderer::uploadImage(const void* pixels, VkExtent2D extent,
                                                  VkFormat format, VkDeviceSize bytes,
                                                  uint32_t mipLevels, VkCommandBuffer command,
                                                  VkFence fence, std::span<const size_t> mipOffsets)
{
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(physicalDevice_, &deviceProperties);
    if (!extent.width || !extent.height ||
        extent.width > deviceProperties.limits.maxImageDimension2D ||
        extent.height > deviceProperties.limits.maxImageDimension2D)
        throw std::runtime_error("Texture dimensions exceed device limits");
    VkFormatProperties properties;
    vkGetPhysicalDeviceFormatProperties(physicalDevice_, format, &properties);
    VkFormatFeatureFlags required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                    VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
                                    VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    const uint32_t copiedLevels = std::max(1u, static_cast<uint32_t>(mipOffsets.size()));
    const bool needsBlits = copiedLevels < mipLevels;
    if (needsBlits)
        required |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT |
                    VK_FORMAT_FEATURE_BLIT_DST_BIT;
    if ((properties.optimalTilingFeatures & required) != required)
        throw std::runtime_error("Texture format lacks required Vulkan features");

    Buffer staging;
    Image image;
    try
    {
        staging = createBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void* mapped = nullptr;
        check(vkMapMemory(device_, staging.memory, 0, bytes, 0, &mapped),
              "Map texture staging memory");
        std::memcpy(mapped, pixels, static_cast<size_t>(bytes));
        vkUnmapMemory(device_, staging.memory);
        const VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                        VK_IMAGE_USAGE_SAMPLED_BIT |
                                        (needsBlits ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
        image = createImage(extent, format, usage, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels);
        beginUpload(command);
        VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image.handle;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1};
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(command, &dependency);
        std::vector<VkBufferImageCopy> copies(copiedLevels);
        for (uint32_t level = 0; level < copiedLevels; ++level)
        {
            auto& copy = copies[level];
            copy.bufferOffset = mipOffsets.empty() ? 0 : mipOffsets[level];
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
            copy.imageExtent = {std::max(extent.width >> level, 1u),
                                std::max(extent.height >> level, 1u), 1};
        }
        vkCmdCopyBufferToImage(command, staging.handle, image.handle,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, copiedLevels, copies.data());
        generateMipmaps(image, extent, copiedLevels, mipLevels, command);
        endUpload(command, fence);
    }
    catch (...)
    {
        destroyBuffer(staging);
        destroyImage(image);
        throw;
    }
    destroyBuffer(staging);
    return image;
}

void VulkanRenderer::createEnvironment()
{
    // Neutral studio lighting with one broad highlight for roughness and metallic previews.
    constexpr VkExtent2D extent{256, 128};
    std::vector<glm::vec4> pixels(extent.width * extent.height);
    for (uint32_t y = 0; y < extent.height; ++y)
        for (uint32_t x = 0; x < extent.width; ++x)
        {
            const float u = (float(x) + 0.5f) / float(extent.width);
            const float v = (float(y) + 0.5f) / float(extent.height);
            const float azimuth = glm::cos((u - 0.25f) * glm::radians(360.0f));
            const float highlight = glm::pow(std::max(azimuth, 0.0f), 16.0f) *
                                    glm::exp(-80.0f * (v - 0.25f) * (v - 0.25f));
            const auto base = glm::mix(glm::vec3(0.16f, 0.19f, 0.24f), glm::vec3(0.035f), v);
            pixels[y * extent.width + x] = glm::vec4(base + glm::vec3(2.5f * highlight), 1.0f);
        }
    environment_ = uploadImage(pixels.data(), extent, VK_FORMAT_R32G32B32A32_SFLOAT,
                               pixels.size() * sizeof(glm::vec4), std::bit_width(extent.width),
                               commandBuffer_);
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.maxLod = VK_LOD_CLAMP_NONE;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    check(vkCreateSampler(device_, &sampler, nullptr, &environmentSampler_),
          "Create environment sampler");
}

VulkanRenderer::Image VulkanRenderer::precomputeEnvironment(const char* shaderFilename,
                                                            VkExtent2D extent, uint32_t mipLevels,
                                                            bool sheen)
{
    struct Parameters
    {
        float roughness;
        uint32_t sheen;
    };
    constexpr VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
    VkFormatProperties properties;
    vkGetPhysicalDeviceFormatProperties(physicalDevice_, format, &properties);
    constexpr VkFormatFeatureFlags required = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
                                              VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                              VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((properties.optimalTilingFeatures & required) != required)
        throw std::runtime_error("RGBA32F storage and linear sampling are unsupported");
    constexpr VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    std::vector<VkImageView> mipViews(mipLevels);
    Image image = createImage(extent, format, usage, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels);

    VkDescriptorSetLayout layout{};
    VkDescriptorPool pool{};
    VkPipelineLayout pipelineLayout{};
    VkPipeline pipeline{};
    VkShaderModule shader{};
    const auto release = [&]
    {
        vkDestroyPipeline(device_, pipeline, nullptr);
        vkDestroyPipelineLayout(device_, pipelineLayout, nullptr);
        vkDestroyShaderModule(device_, shader, nullptr);
        vkDestroyDescriptorPool(device_, pool, nullptr);
        vkDestroyDescriptorSetLayout(device_, layout, nullptr);
        for (auto view : mipViews)
            vkDestroyImageView(device_, view, nullptr);
    };
    try
    {
        const std::array<VkDescriptorSetLayoutBinding, 2> bindings{{
            {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        }};
        VkDescriptorSetLayoutCreateInfo layoutInfo{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        check(vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &layout),
              "Create precompute layout");
        const std::array<VkDescriptorPoolSize, 2> sizes{{
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, mipLevels},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, mipLevels},
        }};
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = mipLevels;
        poolInfo.poolSizeCount = static_cast<uint32_t>(sizes.size());
        poolInfo.pPoolSizes = sizes.data();
        check(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &pool), "Create precompute pool");
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = pool;
        allocate.descriptorSetCount = mipLevels;
        const std::vector<VkDescriptorSetLayout> layouts(mipLevels, layout);
        allocate.pSetLayouts = layouts.data();
        std::vector<VkDescriptorSet> descriptors(mipLevels);
        check(vkAllocateDescriptorSets(device_, &allocate, descriptors.data()),
              "Allocate precompute sets");
        const VkDescriptorImageInfo input{environmentSampler_, environment_.view,
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        for (uint32_t level = 0; level < mipLevels; ++level)
        {
            // Storage views expose one mip; each dispatch keeps its own descriptor set.
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view.image = image.handle;
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = format;
            view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, level, 1, 0, 1};
            check(vkCreateImageView(device_, &view, nullptr, &mipViews[level]),
                  "Create precompute mip view");
            const VkDescriptorImageInfo output{VK_NULL_HANDLE, mipViews[level],
                                               VK_IMAGE_LAYOUT_GENERAL};
            std::array<VkWriteDescriptorSet, 2> writes{};
            for (uint32_t i = 0; i < writes.size(); ++i)
            {
                writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[i].dstSet = descriptors[level];
                writes[i].dstBinding = i;
                writes[i].descriptorCount = 1;
            }
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[0].pImageInfo = &input;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[1].pImageInfo = &output;
            vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0,
                                   nullptr);
        }
        const VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Parameters)};
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{
            VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &layout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &push;
        check(vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &pipelineLayout),
              "Create precompute pipeline layout");
        shader = loadShader(device_, shaderFilename);
        VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        info.stage.module = shader;
        info.stage.pName = "computeMain";
        info.layout = pipelineLayout;
        check(vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
              "Create precompute pipeline");

        beginUpload(commandBuffer_);
        VkImageMemoryBarrier2 source{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        source.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        source.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        source.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        source.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        source.oldLayout = source.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        source.srcQueueFamilyIndex = source.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        source.image = environment_.handle;
        source.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, 1};
        VkImageMemoryBarrier2 destination = source;
        destination.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
        destination.srcAccessMask = 0;
        destination.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        destination.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        destination.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        destination.image = image.handle;
        destination.subresourceRange.levelCount = mipLevels;
        const std::array barriers{source, destination};
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
        dependency.pImageMemoryBarriers = barriers.data();
        vkCmdPipelineBarrier2(commandBuffer_, &dependency);
        vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        for (uint32_t level = 0; level < mipLevels; ++level)
        {
            const Parameters parameters{mipLevels > 1 ? float(level) / float(mipLevels - 1) : 0.0f,
                                        sheen ? 1u : 0u};
            vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout,
                                    0, 1, &descriptors[level], 0, nullptr);
            vkCmdPushConstants(commandBuffer_, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                               sizeof(parameters), &parameters);
            vkCmdDispatch(commandBuffer_, (std::max(extent.width >> level, 1u) + 7) / 8,
                          (std::max(extent.height >> level, 1u) + 7) / 8, 1);
        }
        destination.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        destination.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        destination.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        destination.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        destination.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        destination.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &destination;
        vkCmdPipelineBarrier2(commandBuffer_, &dependency);
        endUpload(commandBuffer_);
    }
    catch (...)
    {
        release();
        destroyImage(image);
        throw;
    }
    release();
    std::cout << shaderFilename << ": " << extent.width << 'x' << extent.height << ", " << mipLevels
              << " mip(s)\n";
    return image;
}

void VulkanRenderer::generateMipmaps(const Image& image, VkExtent2D extent, uint32_t copiedLevels,
                                     uint32_t mipLevels, VkCommandBuffer command)
{
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image.handle;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    const auto finishLevels = [&](uint32_t first, uint32_t count)
    {
        barrier.subresourceRange.baseMipLevel = first;
        barrier.subresourceRange.levelCount = count;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        vkCmdPipelineBarrier2(command, &dependency);
    };
    if (copiedLevels == mipLevels)
    {
        finishLevels(0, mipLevels);
        return;
    }
    if (copiedLevels > 1)
        finishLevels(0, copiedLevels - 1);

    const uint32_t lastCopied = copiedLevels - 1;
    int32_t width = static_cast<int32_t>(std::max(extent.width >> lastCopied, 1u));
    int32_t height = static_cast<int32_t>(std::max(extent.height >> lastCopied, 1u));
    barrier.subresourceRange.levelCount = 1;
    for (uint32_t level = copiedLevels; level < mipLevels; ++level)
    {
        // Use the last supplied level as the source for the missing tail.
        barrier.subresourceRange.baseMipLevel = level - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier2(command, &dependency);
        const int32_t nextWidth = std::max(width / 2, 1), nextHeight = std::max(height / 2, 1);
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1};
        blit.srcOffsets[1] = {width, height, 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
        blit.dstOffsets[1] = {nextWidth, nextHeight, 1};
        vkCmdBlitImage(command, image.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image.handle,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        vkCmdPipelineBarrier2(command, &dependency);
        width = nextWidth;
        height = nextHeight;
    }
    finishLevels(mipLevels - 1, 1);
}

int VulkanRenderer::shadowLightIndex() const
{
    const int index = settings.shadowLight;
    if (!settings.localShadows || index < 0 || index >= int(settings.lights.size()))
        return -1;
    const auto& light = settings.lights[index];
    return light.enabled && light.intensity > 0.0f && light.range > 0.0f ? index : -1;
}

void VulkanRenderer::createShadowResources()
{
    constexpr VkFormatFeatureFlags required =
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    for (auto format : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM})
    {
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(physicalDevice_, format, &properties);
        VkImageFormatProperties cube;
        if ((properties.optimalTilingFeatures & required) == required &&
            vkGetPhysicalDeviceImageFormatProperties(
                physicalDevice_, format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, &cube) == VK_SUCCESS)
        {
            depthFormat_ = format;
            break;
        }
    }
    if (!depthFormat_)
        throw std::runtime_error("No supported sampled depth format");
    const auto sampleCounts = [&](VkFormat format, VkImageUsageFlags usage)
    {
        VkImageFormatProperties properties;
        check(vkGetPhysicalDeviceImageFormatProperties(physicalDevice_, format, VK_IMAGE_TYPE_2D,
                                                       VK_IMAGE_TILING_OPTIMAL, usage, 0,
                                                       &properties),
              "Query image sample counts");
        return properties.sampleCounts;
    };
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(physicalDevice_, &deviceProperties);
    const auto supported = deviceProperties.limits.framebufferColorSampleCounts &
                           deviceProperties.limits.framebufferDepthSampleCounts &
                           sampleCounts(hdrFormat, msaaColorUsage) &
                           sampleCounts(depthFormat_, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
    msaaSamples_ = supported & VK_SAMPLE_COUNT_4_BIT   ? VK_SAMPLE_COUNT_4_BIT
                   : supported & VK_SAMPLE_COUNT_2_BIT ? VK_SAMPLE_COUNT_2_BIT
                                                       : VK_SAMPLE_COUNT_1_BIT;
    VkImageFormatProperties selectionProperties;
    const auto selectionSupport = vkGetPhysicalDeviceImageFormatProperties(
        physicalDevice_, VK_FORMAT_R8_UNORM, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
        msaaColorUsage, 0, &selectionProperties);
    selectionFormat_ =
        selectionSupport == VK_SUCCESS && (selectionProperties.sampleCounts & msaaSamples_)
            ? VK_FORMAT_R8_UNORM
            : hdrFormat;
    shadowMap_ =
        createImage(shadowExtent, depthFormat_,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_DEPTH_BIT);
    pointShadowMap_ =
        createImage(pointShadowExtent, depthFormat_,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_DEPTH_BIT, 1, VK_SAMPLE_COUNT_1_BIT, true);
    spotShadowMap_ =
        createImage(pointShadowExtent, depthFormat_,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_DEPTH_BIT);
    for (uint32_t face = 0; face < pointShadowFaces_.size(); ++face)
    {
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = pointShadowMap_.handle;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = depthFormat_;
        view.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, face, 1};
        check(vkCreateImageView(device_, &view, nullptr, &pointShadowFaces_[face]),
              "Create point shadow face");
    }
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    check(vkCreateSampler(device_, &sampler, nullptr, &shadowSampler_), "Create shadow sampler");
    // All descriptors need valid layouts, including the inactive local shadow image.
    beginUpload(commandBuffer_);
    std::array<VkImageMemoryBarrier2, 3> barriers{};
    const std::array images{shadowMap_.handle, pointShadowMap_.handle, spotShadowMap_.handle};
    for (size_t i = 0; i < barriers.size(); ++i)
    {
        auto& barrier = barriers[i];
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = images[i];
        barrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, i == 1 ? 6u : 1u};
    }
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
    dependency.pImageMemoryBarriers = barriers.data();
    vkCmdPipelineBarrier2(commandBuffer_, &dependency);
    endUpload(commandBuffer_);
}

void VulkanRenderer::createDescriptors()
{
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.maxLod = VK_LOD_CLAMP_NONE;
    check(vkCreateSampler(device_, &sampler, nullptr, &sampler_), "Create sampler");
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.maxLod = 0.0f;
    check(vkCreateSampler(device_, &sampler, nullptr, &clampSampler_), "Create clamp sampler");
    sampler.maxLod = VK_LOD_CLAMP_NONE;
    check(vkCreateSampler(device_, &sampler, nullptr, &transmissionSampler_),
          "Create transmission sampler");
    sceneBuffer_ =
        createBuffer(sizeof(SceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkMapMemory(device_, sceneBuffer_.memory, 0, sizeof(SceneData), 0, &sceneMapped_),
          "Map scene uniform buffer");
    emptyGeometryBuffer_ = createBuffer(sizeof(glm::vec4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    std::array<VkDescriptorSetLayoutBinding, materialBindingCount> bindings{{
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {11, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {12, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
    }};
    for (uint32_t i = advancedTextureBase; i < bindings.size(); ++i)
        bindings[i] = {i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                       VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout.bindingCount = static_cast<uint32_t>(bindings.size());
    layout.pBindings = bindings.data();
    check(vkCreateDescriptorSetLayout(device_, &layout, nullptr, &descriptorLayout_),
          "Create descriptor layout");
    const std::array postprocessBindings{bindings[0], bindings[1], bindings[2], bindings[3]};
    layout.bindingCount = static_cast<uint32_t>(postprocessBindings.size());
    layout.pBindings = postprocessBindings.data();
    check(vkCreateDescriptorSetLayout(device_, &layout, nullptr, &postprocessDescriptorLayout_),
          "Create postprocess descriptor layout");
    layout.bindingCount = 1;
    layout.pBindings = bindings.data();
    check(vkCreateDescriptorSetLayout(device_, &layout, nullptr, &bloomDescriptorLayout_),
          "Create Bloom descriptor layout");
    const std::array<VkDescriptorSetLayoutBinding, 3> poseBindings{{
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    }};
    layout.bindingCount = static_cast<uint32_t>(poseBindings.size());
    layout.pBindings = poseBindings.data();
    check(vkCreateDescriptorSetLayout(device_, &layout, nullptr, &jointLayout_),
          "Create pose layout");
    const std::array<VkDescriptorPoolSize, 3> sizes{{
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, materialSamplerCount + 8},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4},
    }};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = 8;
    pool.poolSizeCount = static_cast<uint32_t>(sizes.size());
    pool.pPoolSizes = sizes.data();
    check(vkCreateDescriptorPool(device_, &pool, nullptr, &descriptorPool_),
          "Create descriptor pool");
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = descriptorPool_;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &jointLayout_;
    check(vkAllocateDescriptorSets(device_, &allocate, &jointSet_), "Allocate pose set");
    allocate.pSetLayouts = &postprocessDescriptorLayout_;
    check(vkAllocateDescriptorSets(device_, &allocate, &postprocessSet_),
          "Allocate postprocess set");
    const VkDescriptorBufferInfo buffer{sceneBuffer_.handle, 0, sizeof(SceneData)};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = postprocessSet_;
    write.dstBinding = 1;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &buffer;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    const std::array bloomLayouts{bloomDescriptorLayout_, bloomDescriptorLayout_,
                                  bloomDescriptorLayout_};
    allocate.descriptorSetCount = static_cast<uint32_t>(bloomSets_.size());
    allocate.pSetLayouts = bloomLayouts.data();
    check(vkAllocateDescriptorSets(device_, &allocate, bloomSets_.data()), "Allocate Bloom sets");
    const std::array transmissionLayouts{bloomDescriptorLayout_, bloomDescriptorLayout_};
    allocate.descriptorSetCount = static_cast<uint32_t>(transmissionSets_.size());
    allocate.pSetLayouts = transmissionLayouts.data();
    check(vkAllocateDescriptorSets(device_, &allocate, transmissionSets_.data()),
          "Allocate transmission sets");
    writeSampledImage(transmissionSets_[0], 0, environment_.view, transmissionSampler_);
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &descriptorLayout_;
    check(vkAllocateDescriptorSets(device_, &allocate, &skySet_), "Allocate sky set");
    const VkDescriptorImageInfo sky{environmentSampler_, environment_.view,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    auto skyBuffer = write;
    skyBuffer.dstSet = skySet_;
    VkWriteDescriptorSet skyImage{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    skyImage.dstSet = skySet_;
    skyImage.dstBinding = 4;
    skyImage.descriptorCount = 1;
    skyImage.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    skyImage.pImageInfo = &sky;
    const std::array skyWrites{skyBuffer, skyImage};
    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(skyWrites.size()), skyWrites.data(), 0,
                           nullptr);
}

void VulkanRenderer::writeSampledImage(VkDescriptorSet set, uint32_t binding, VkImageView view,
                                       VkSampler sampler)
{
    const VkDescriptorImageInfo image{sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set;
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
}

void VulkanRenderer::createModelDescriptors(ModelResources& model)
{
    const auto count = static_cast<uint32_t>(model.materials.size());
    const std::array<VkDescriptorPoolSize, 3> sizes{{
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, count * materialSamplerCount},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, count},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, count},
    }};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = count;
    pool.poolSizeCount = static_cast<uint32_t>(sizes.size());
    pool.pPoolSizes = sizes.data();
    check(vkCreateDescriptorPool(device_, &pool, nullptr, &model.pool), "Create material pool");
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = model.pool;
    allocate.descriptorSetCount = count;
    const std::vector<VkDescriptorSetLayout> layouts(count, descriptorLayout_);
    allocate.pSetLayouts = layouts.data();
    model.sets.resize(count);
    check(vkAllocateDescriptorSets(device_, &allocate, model.sets.data()),
          "Allocate material descriptor sets");
    const VkDescriptorBufferInfo buffer{sceneBuffer_.handle, 0, sizeof(SceneData)};
    const VkDescriptorBufferInfo geometry{model.geometryData.handle ? model.geometryData.handle
                                                                    : emptyGeometryBuffer_.handle,
                                          0, VK_WHOLE_SIZE};
    const auto lightingView = model.environment.view;
    const VkDescriptorImageInfo environment{environmentSampler_,
                                            lightingView ? lightingView : environment_.view,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo irradiance{environmentSampler_,
                                           lightingView ? model.irradiance.view : irradiance_.view,
                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo prefiltered{
        environmentSampler_, lightingView ? lightingView : prefilteredEnvironment_.view,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo sheenEnvironment{
        environmentSampler_, lightingView ? lightingView : prefilteredSheen_.view,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo brdf{clampSampler_, brdfLut_.view,
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo shadow{shadowSampler_, shadowMap_.view,
                                       VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo pointShadow{shadowSampler_, pointShadowMap_.view,
                                            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo spotShadow{shadowSampler_, spotShadowMap_.view,
                                           VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    const auto getSampler = [&](const TextureSampler& state)
    {
        if (state == TextureSampler{})
            return sampler_;
        const auto found = std::ranges::find_if(model.samplers,
                                                [&](const auto& entry)
                                                {
                                                    return entry.first == state;
                                                });
        if (found != model.samplers.end())
            return found->second;

        const auto addressMode = [](TextureWrap wrap)
        {
            switch (wrap)
            {
            case TextureWrap::MirroredRepeat:
                return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            case TextureWrap::ClampToEdge:
                return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            default:
                return VK_SAMPLER_ADDRESS_MODE_REPEAT;
            }
        };
        VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        info.magFilter = state.nearestMag ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        info.minFilter = state.nearestMin ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        info.mipmapMode = state.mip == MipFilter::Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                                                         : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.addressModeU = addressMode(state.wrapU);
        info.addressModeV = addressMode(state.wrapV);
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        // A positive clamp keeps minification active while choosing mip level zero.
        info.maxLod = state.mip == MipFilter::None ? 0.25f : VK_LOD_CLAMP_NONE;
        auto& handle = model.samplers.emplace_back(state, VK_NULL_HANDLE).second;
        check(vkCreateSampler(device_, &info, nullptr, &handle), "Create material sampler");
        return handle;
    };
    for (size_t i = 0; i < model.materials.size(); ++i)
    {
        const auto& material = model.materials[i];
        std::array<VkDescriptorImageInfo, MaterialTextureCount> materialImages;
        for (size_t slot = 0; slot < materialImages.size(); ++slot)
        {
            const auto& texture = material.textures[slot];
            materialImages[slot] = {getSampler(texture.sampler), model.textures[texture.image].view,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        }
        std::array<const VkDescriptorImageInfo*, materialBindingCount> images{
            &materialImages[BaseColorSlot],
            nullptr,
            &materialImages[NormalSlot],
            &materialImages[MetallicRoughnessSlot],
            &environment,
            &irradiance,
            &prefiltered,
            &brdf,
            &materialImages[OcclusionSlot],
            &shadow,
            &materialImages[EmissiveSlot],
            &pointShadow,
            &spotShadow};
        for (uint32_t binding = advancedTextureBase; binding < sheenEnvironmentBinding; ++binding)
            images[binding] = &materialImages[BaseTextureCount + binding - advancedTextureBase];
        images[sheenEnvironmentBinding] = &sheenEnvironment;
        std::array<VkWriteDescriptorSet, materialBindingCount> writes{};
        for (uint32_t binding = 0; binding < images.size(); ++binding)
        {
            auto& write = writes[binding];
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = model.sets[i];
            write.dstBinding = binding;
            write.descriptorCount = 1;
            write.descriptorType = binding == 1    ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                   : binding == 13 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                                                   : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pBufferInfo = binding == 1 ? &buffer : binding == 13 ? &geometry : nullptr;
            write.pImageInfo = images[binding];
        }
        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0,
                               nullptr);
    }
}

void VulkanRenderer::createPipeline(PipelineType type)
{
    const bool transparent = type == PipelineType::Transparent;
    const bool mesh = type == PipelineType::Mesh || transparent;
    const bool selection = type == PipelineType::Selection;
    const bool shadow = type == PipelineType::Shadow;
    const bool geometry = mesh || selection || shadow;
    const bool bloom = type == PipelineType::Bloom;
    const bool postprocess = type == PipelineType::Postprocess;
    auto& pipelineLayout = bloom         ? bloomPipelineLayout_
                           : postprocess ? postprocessPipelineLayout_
                                         : pipelineLayout_;
    if (!pipelineLayout)
    {
        const VkPushConstantRange push{
            VkShaderStageFlags(bloom ? VK_SHADER_STAGE_FRAGMENT_BIT
                                     : VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT),
            0, bloom ? uint32_t(sizeof(glm::vec4)) : uint32_t(sizeof(PushConstants))};
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        const std::array sceneLayouts{descriptorLayout_, jointLayout_, bloomDescriptorLayout_};
        layout.setLayoutCount =
            bloom || postprocess ? 1 : static_cast<uint32_t>(sceneLayouts.size());
        layout.pSetLayouts = bloom         ? &bloomDescriptorLayout_
                             : postprocess ? &postprocessDescriptorLayout_
                                           : sceneLayouts.data();
        layout.pushConstantRangeCount = postprocess ? 0 : 1;
        layout.pPushConstantRanges = &push;
        check(vkCreatePipelineLayout(device_, &layout, nullptr, &pipelineLayout),
              "Create pipeline layout");
    }

    std::array<VkShaderModule, 2> modules{};
    try
    {
        const std::string shader = shadow              ? "shadow"
                                   : mesh || selection ? "mesh"
                                   : bloom             ? "bloom"
                                   : postprocess       ? "postprocess"
                                                       : "sky";
        modules[0] = loadShader(device_, (shader + ".vertex.spv").c_str());
        modules[1] = loadShader(device_, selection ? "selection.fragment.spv"
                                                   : (shader + ".fragment.spv").c_str());
        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        for (auto& stage : stages)
            stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = modules[0];
        stages[0].pName = "vertexMain";
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = modules[1];
        stages[1].pName = selection ? "selectionMain" : "fragmentMain";

        const VkVertexInputBindingDescription binding{0, sizeof(Vertex),
                                                      VK_VERTEX_INPUT_RATE_VERTEX};
        static_assert(sizeof(glm::vec2) == 2 * sizeof(float));
        const std::array<VkVertexInputAttributeDescription, 11> attributes{{
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
            {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
            {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color)},
            {3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
            {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tangent)},
            {5, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
             offsetof(Vertex, uv) + NormalSlot * sizeof(glm::vec2)},
            {6, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
             offsetof(Vertex, uv) + OcclusionSlot * sizeof(glm::vec2)},
            {7, 0, VK_FORMAT_R16G16B16A16_UINT, offsetof(Vertex, joints)},
            {8, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, weights)},
            {9, 0, VK_FORMAT_R32G32_UINT, offsetof(Vertex, morph)},
            {10, 0, VK_FORMAT_R32_UINT, offsetof(Vertex, advancedUv)},
        }};
        const std::array shadowAttributes{attributes[0], attributes[1], attributes[2],
                                          attributes[7], attributes[8], attributes[9]};
        VkPipelineVertexInputStateCreateInfo vertex{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertex.vertexBindingDescriptionCount = geometry ? 1 : 0;
        vertex.pVertexBindingDescriptions = &binding;
        vertex.vertexAttributeDescriptionCount =
            shadow     ? static_cast<uint32_t>(shadowAttributes.size())
            : geometry ? static_cast<uint32_t>(attributes.size())
                       : 0;
        vertex.pVertexAttributeDescriptions = shadow ? shadowAttributes.data() : attributes.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo samples{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        samples.rasterizationSamples = (mesh || selection || type == PipelineType::Sky)
                                           ? sceneSamples_
                                           : VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable = geometry;
        depth.depthWriteEnable = geometry && !transparent && !selection;
        depth.depthCompareOp = selection ? VK_COMPARE_OP_EQUAL : VK_COMPARE_OP_LESS;
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.blendEnable = transparent;
        attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        attachment.colorBlendOp = VK_BLEND_OP_ADD;
        attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        if (selection)
            attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
        VkPipelineColorBlendStateCreateInfo blend{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = shadow ? 0 : 1;
        blend.pAttachments = &attachment;
        const std::array states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                VK_DYNAMIC_STATE_CULL_MODE, VK_DYNAMIC_STATE_FRONT_FACE};
        VkPipelineDynamicStateCreateInfo dynamic{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = geometry ? 4 : 2;
        dynamic.pDynamicStates = states.data();
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = shadow ? 0 : 1;
        rendering.pColorAttachmentFormats = postprocess ? &format_
                                            : selection ? &selectionFormat_
                                                        : &hdrFormat;
        rendering.depthAttachmentFormat =
            (postprocess || bloom) ? VK_FORMAT_UNDEFINED : depthFormat_;
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.pNext = &rendering;
        info.stageCount = static_cast<uint32_t>(stages.size());
        info.pStages = stages.data();
        info.pVertexInputState = &vertex;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &samples;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = pipelineLayout;
        check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr,
                                        shadow        ? &shadowPipeline_
                                        : transparent ? &transparentPipeline_
                                        : selection   ? &selectionPipeline_
                                        : mesh        ? &pipeline_
                                        : bloom       ? &bloomPipeline_
                                        : postprocess ? &postprocessPipeline_
                                                      : &skyPipeline_),
              "Create graphics pipeline");
    }
    catch (...)
    {
        for (auto module : modules)
            vkDestroyShaderModule(device_, module, nullptr);
        throw;
    }
    // Pipeline creation consumes the shader code; modules need not stay alive.
    for (auto module : modules)
        vkDestroyShaderModule(device_, module, nullptr);
}

void VulkanRenderer::createRenderTargets()
{
    // Resize and MSAA changes arrive here after vkDeviceWaitIdle.
    destroyRenderTargets();
    const auto samples = settings.msaa ? msaaSamples_ : VK_SAMPLE_COUNT_1_BIT;
    if (samples != sceneSamples_)
    {
        vkDestroyPipeline(device_, pipeline_, nullptr);
        vkDestroyPipeline(device_, transparentPipeline_, nullptr);
        vkDestroyPipeline(device_, selectionPipeline_, nullptr);
        vkDestroyPipeline(device_, skyPipeline_, nullptr);
        pipeline_ = transparentPipeline_ = selectionPipeline_ = skyPipeline_ = VK_NULL_HANDLE;
        sceneSamples_ = samples;
    }
    depth_ = createImage(extent_, depthFormat_, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                         VK_IMAGE_ASPECT_DEPTH_BIT, 1, sceneSamples_);
    if (sceneSamples_ != VK_SAMPLE_COUNT_1_BIT)
        msaaColor_ = createImage(extent_, hdrFormat, msaaColorUsage, VK_IMAGE_ASPECT_COLOR_BIT, 1,
                                 sceneSamples_);
    VkFormatProperties properties;
    vkGetPhysicalDeviceFormatProperties(physicalDevice_, hdrFormat, &properties);
    constexpr VkFormatFeatureFlags required =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
        VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
    if ((properties.optimalTilingFeatures & required) != required)
        throw std::runtime_error("RGBA16F color blending, sampling or blitting is unsupported");
    hdrColor_ = createImage(extent_, hdrFormat,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                            VK_IMAGE_ASPECT_COLOR_BIT);
    selectionMask_ = createImage(extent_, selectionFormat_,
                                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                 VK_IMAGE_ASPECT_COLOR_BIT);
    if (sceneSamples_ != VK_SAMPLE_COUNT_1_BIT)
        msaaSelectionMask_ = createImage(extent_, selectionFormat_, msaaColorUsage,
                                         VK_IMAGE_ASPECT_COLOR_BIT, 1, sceneSamples_);
    bloomExtent_ = {std::max((extent_.width + 1) / 2, 1u), std::max((extent_.height + 1) / 2, 1u)};
    for (auto& image : bloomImages_)
        image = createImage(bloomExtent_, hdrFormat,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_IMAGE_ASPECT_COLOR_BIT);
    writeSampledImage(postprocessSet_, 0, hdrColor_.view, clampSampler_);
    writeSampledImage(postprocessSet_, 2, bloomImages_[0].view, clampSampler_);
    writeSampledImage(postprocessSet_, 3, selectionMask_.view, clampSampler_);
    for (uint32_t i = 0; i < bloomSets_.size(); ++i)
        writeSampledImage(bloomSets_[i], 0, i == 0 ? hdrColor_.view : bloomImages_[i - 1].view,
                          clampSampler_);
    if (!pipeline_)
        createPipeline(PipelineType::Mesh);
    if (!transparentPipeline_)
        createPipeline(PipelineType::Transparent);
    if (!selectionPipeline_)
        createPipeline(PipelineType::Selection);
    if (!skyPipeline_)
        createPipeline(PipelineType::Sky);
}

void VulkanRenderer::createSwapchain()
{
    int width, height;
    glfwGetFramebufferSize(window_, &width, &height);
    while (width == 0 || height == 0 || glfwGetWindowAttrib(window_, GLFW_ICONIFIED))
    {
        if (glfwWindowShouldClose(window_))
            return;
        glfwWaitEvents();
        glfwGetFramebufferSize(window_, &width, &height);
    }
    // Recreation waits for all queues; ordinary frames use a fence.
    {
        auto lock = lockQueues();
        check(vkDeviceWaitIdle(device_), "Wait before swapchain recreation");
    }
    destroySwapchain();
    VkSurfaceCapabilitiesKHR capabilities;
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &capabilities),
          "Query surface capabilities");
    if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
        throw std::runtime_error("Surface does not support color attachments");
    uint32_t count = 0;
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &count, nullptr),
          "Query surface formats");
    std::vector<VkSurfaceFormatKHR> formats(count);
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &count, formats.data()),
          "Query surface formats");
    if (formats.empty())
        throw std::runtime_error("Surface has no supported format");
    auto selected = formats.front();
    for (auto format : formats)
        if ((format.format == VK_FORMAT_B8G8R8A8_SRGB ||
             format.format == VK_FORMAT_R8G8B8A8_SRGB) &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            selected = format;
            if (format.format == VK_FORMAT_B8G8R8A8_SRGB)
                break;
        }
    if (selected.format == VK_FORMAT_UNDEFINED)
        selected.format = VK_FORMAT_B8G8R8A8_SRGB;
    if ((selected.format != VK_FORMAT_B8G8R8A8_SRGB &&
         selected.format != VK_FORMAT_R8G8B8A8_SRGB) ||
        selected.colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        throw std::runtime_error("An sRGB swapchain format is required");
    const bool formatChanged = format_ != selected.format;
    format_ = selected.format;
    extent_ = capabilities.currentExtent;
    if (extent_.width == UINT32_MAX)
    {
        extent_.width = std::clamp(static_cast<uint32_t>(width), capabilities.minImageExtent.width,
                                   capabilities.maxImageExtent.width);
        extent_.height =
            std::clamp(static_cast<uint32_t>(height), capabilities.minImageExtent.height,
                       capabilities.maxImageExtent.height);
    }
    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount)
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface_;
    info.minImageCount = imageCount;
    info.imageFormat = format_;
    info.imageColorSpace = selected.colorSpace;
    info.imageExtent = extent_;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    const std::array families{graphicsFamily_, presentFamily_};
    info.imageSharingMode =
        graphicsFamily_ == presentFamily_ ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT;
    if (info.imageSharingMode == VK_SHARING_MODE_CONCURRENT)
    {
        info.queueFamilyIndexCount = 2;
        info.pQueueFamilyIndices = families.data();
    }
    info.preTransform = capabilities.currentTransform;
    for (auto alpha :
         {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
          VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
        if (capabilities.supportedCompositeAlpha & alpha)
        {
            info.compositeAlpha = alpha;
            break;
        }
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    check(vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_), "Create swapchain");
    check(vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr), "Get swapchain images");
    images_.resize(count);
    check(vkGetSwapchainImagesKHR(device_, swapchain_, &count, images_.data()),
          "Get swapchain images");
    views_.resize(count);
    rendered_.resize(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = images_[i];
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format_;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        check(vkCreateImageView(device_, &view, nullptr, &views_[i]), "Create image view");
        VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        check(vkCreateSemaphore(device_, &semaphore, nullptr, &rendered_[i]),
              "Create present semaphore");
    }
    createRenderTargets();
    if (!bloomPipeline_)
        createPipeline(PipelineType::Bloom);
    if (!shadowPipeline_)
        createPipeline(PipelineType::Shadow);
    if (!postprocessPipeline_ || formatChanged)
    {
        vkDestroyPipeline(device_, postprocessPipeline_, nullptr);
        postprocessPipeline_ = VK_NULL_HANDLE;
        createPipeline(PipelineType::Postprocess);
    }
    resized_ = false;
}

void* VulkanRenderer::mapPoseBuffer(Buffer& buffer, VkDeviceSize& capacity, VkDeviceSize bytes,
                                    uint32_t binding, bool hasData)
{
    if (bytes > capacity)
    {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
        if (bytes > properties.limits.maxStorageBufferRange)
            throw std::runtime_error("Scene pose exceeds the device storage buffer limit");
        const auto size = std::min<VkDeviceSize>(std::max(bytes, capacity * 2),
                                                 properties.limits.maxStorageBufferRange);
        auto replacement = createBuffer(size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        destroyBuffer(buffer);
        buffer = replacement;
        capacity = size;
        const VkDescriptorBufferInfo info{buffer.handle, 0, size};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = jointSet_;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &info;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }
    if (!hasData)
        return nullptr;
    void* mapped = nullptr;
    check(vkMapMemory(device_, buffer.memory, 0, bytes, 0, &mapped), "Map scene pose buffer");
    return mapped;
}

void VulkanRenderer::uploadPose(std::span<const RenderInstance> instances, size_t jointCount,
                                size_t morphCount, size_t materialCount)
{
    const auto upload = [&]<typename T>(Buffer& buffer, VkDeviceSize& capacity, size_t count,
                                        uint32_t binding, uint32_t RenderInstance::* offset,
                                        std::span<const T> RenderInstance::* values)
    {
        auto* mapped = static_cast<std::byte*>(
            mapPoseBuffer(buffer, capacity, count * sizeof(T), binding, count > 1));
        if (!mapped)
            return;
        for (const auto& instance : instances)
            if (const auto first = instance.*offset)
            {
                const auto data = instance.*values;
                std::memcpy(mapped + first * sizeof(T), data.data(), data.size_bytes());
            }
        vkUnmapMemory(device_, buffer.memory);
    };
    upload(jointBuffer_, jointBufferSize_, jointCount, 0, &RenderInstance::jointOffset,
           &RenderInstance::joints);
    upload(morphWeightBuffer_, morphWeightBufferSize_, morphCount, 1, &RenderInstance::morphOffset,
           &RenderInstance::weights);
    auto* mapped = static_cast<std::byte*>(mapPoseBuffer(materialBuffer_, materialBufferSize_,
                                                         materialCount * sizeof(GpuMaterial), 2,
                                                         materialCount > 1));
    if (!mapped)
        return;
    for (const auto& instance : instances)
        for (const auto& material : instance.materials)
            if (material.gpuIndex)
            {
                GpuMaterial gpu{};
                gpu.advanced = material.advanced;
                if (material.animatedUv)
                    for (size_t slot = 0; slot < gpu.uv.size(); ++slot)
                    {
                        const auto& uv = material.uv[slot];
                        const float c = std::cos(uv.rotation), s = std::sin(uv.rotation);
                        gpu.uv[slot] = {
                            {c * uv.scale.x, -s * uv.scale.x, s * uv.scale.y, c * uv.scale.y},
                            glm::vec4(uv.offset, 0.0f, 0.0f)};
                    }
                const size_t index = instance.materialOffset + material.gpuIndex;
                std::memcpy(mapped + index * sizeof(gpu), &gpu, sizeof(gpu));
            }
    vkUnmapMemory(device_, materialBuffer_.memory);
}

void VulkanRenderer::recordGeometry(std::span<const RenderInstance> scene, GeometryPass pass,
                                    const glm::mat4& view, int shadowView)
{
    vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 1, 1,
                            &jointSet_, 0, nullptr);
    const bool transparent = pass == GeometryPass::Blend;
    const auto matches = [&](const RenderInstance& instance, const Draw& part)
    {
        const auto& source = *instance.source;
        const auto& material = models_[source.model.index].materials[part.materialIndex];
        if (!source.nodeVisible(part.node) || !part.usesVariant(source.materialVariant) ||
            (material.alphaMode == AlphaMode::Blend) != transparent)
            return false;
        const bool transmission =
            instance.materials[part.materialIndex].advanced.transmission.x > 0.0f;
        if (pass == GeometryPass::Transmission)
            return transmission;
        if (pass == GeometryPass::Opaque || pass == GeometryPass::Shadow)
            return !transmission;
        return true;
    };
    const auto draw = [&](const RenderInstance& instance, const Draw& part)
    {
        const auto& source = *instance.source;
        const auto& model = models_[source.model.index];
        const auto& material = model.materials[part.materialIndex];
        const auto& values = instance.materials[part.materialIndex];
        const auto materialIndex = values.gpuIndex ? instance.materialOffset + values.gpuIndex : 0;
        const bool skinned = part.skin != noSkin;
        const auto transform = skinned ? source.transform : source.nodeTransform(part.node);
        const float determinant = glm::determinant(glm::mat3(transform));
        if (!std::isfinite(determinant))
            return;
        // Cube projections omit the screen Y flip. A mirrored node also reverses winding.
        const bool clockwise = (shadowView >= 0) != (determinant < 0.0f);
        vkCmdSetFrontFace(commandBuffer_,
                          clockwise ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE);
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commandBuffer_, 0, 1, &model.vertices.handle, &offset);
        vkCmdBindIndexBuffer(commandBuffer_, model.indices.handle, 0, VK_INDEX_TYPE_UINT32);
        vkCmdSetCullMode(commandBuffer_,
                         material.doubleSided && values.advanced.transmission.y == 0.0f
                             ? VK_CULL_MODE_NONE
                             : VK_CULL_MODE_BACK_BIT);
        const auto descriptor = model.sets[part.materialIndex];
        vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0,
                                1, &descriptor, 0, nullptr);
        const PushConstants constants{
            transform,
            values.baseColorFactor,
            glm::vec4(values.normalScale, values.metallicFactor, values.roughnessFactor,
                      settings.ambientOcclusion ? values.occlusionStrength : 0.0f),
            values.emissiveFactor * values.emissiveStrength *
                (settings.emissive ? settings.emissiveStrength : 0.0f),
            skinned ? instance.jointOffset + part.skin : 0,
            material.alphaMode == AlphaMode::Mask ? values.alphaCutoff : -1.0f,
            shadowView,
            (transparent ? blendFlag : 0) | (material.unlit ? unlitFlag : 0) |
                (part.flatMorphNormals ? flatMorphNormalsFlag : 0) |
                (part.deriveTangent ? deriveTangentFlag : 0) |
                (values.animatedUv ? animatedUvFlag : 0) | (materialIndex << materialIndexShift),
            part.morphCount
                ? instance.morphOffset + source.model.scene->nodes[part.node].morphOffset
                : 0};
        vkCmdPushConstants(commandBuffer_, pipelineLayout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(constants), &constants);
        vkCmdDrawIndexed(commandBuffer_, part.indexCount, 1, part.firstIndex, part.vertexOffset, 0);
    };
    if (transparent)
    {
        struct TransparentDraw
        {
            float depth;
            const RenderInstance* instance;
            const Draw* part;
        };
        std::vector<TransparentDraw> sorted;
        for (const auto& instance : scene)
        {
            const auto& source = *instance.source;
            const auto& model = models_[source.model.index];
            for (const auto& part : model.draws)
                if (matches(instance, part))
                {
                    const auto center = source.nodeTransform(part.node) * glm::vec4(part.center, 1);
                    sorted.push_back({(view * center).z, &instance, &part});
                }
        }
        // Visible view-space Z is negative: more negative is farther from the camera.
        std::stable_sort(sorted.begin(), sorted.end(),
                         [](const auto& a, const auto& b)
                         {
                             return a.depth < b.depth;
                         });
        for (const auto& part : sorted)
            draw(*part.instance, *part.part);
    }
    else
    {
        for (const auto& instance : scene)
            for (const auto& part : models_[instance.source->model.index].draws)
                if (matches(instance, part))
                    draw(instance, part);
    }
}

void VulkanRenderer::recordShadow(std::span<const RenderInstance> scene, ShadowType type)
{
    const bool point = type == ShadowType::Point, spot = type == ShadowType::Spot;
    const bool local = point || spot;
    if (scene.empty())
        return;
    const auto extent = local ? pointShadowExtent : shadowExtent;
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    barrier.dstStageMask =
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = spot    ? spotShadowMap_.handle
                    : point ? pointShadowMap_.handle
                            : shadowMap_.handle;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, point ? 6u : 1u};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commandBuffer_, &dependency);

    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.clearValue.depthStencil = {1.0f, 0};
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent = extent;
    rendering.layerCount = 1;
    rendering.pDepthAttachment = &depth;
    const VkViewport viewport{0.0f, 0.0f, float(extent.width), float(extent.height), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(commandBuffer_, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer_, 0, 1, &scissor);
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline_);
    for (uint32_t face = 0; face < (point ? 6u : 1u); ++face)
    {
        depth.imageView = spot    ? spotShadowMap_.view
                          : point ? pointShadowFaces_[face]
                                  : shadowMap_.view;
        vkCmdBeginRendering(commandBuffer_, &rendering);
        recordGeometry(scene, GeometryPass::Shadow, glm::mat4(1.0f),
                       spot    ? -2
                       : point ? int(face)
                               : -1);
        vkCmdEndRendering(commandBuffer_);
    }

    barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    barrier.srcStageMask =
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    vkCmdPipelineBarrier2(commandBuffer_, &dependency);
}

void VulkanRenderer::recordSelection(const RenderInstance* selected)
{
    const bool multisampled = selected && msaaSelectionMask_.view;
    VkRenderingAttachmentInfo mask{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    mask.imageView = multisampled ? msaaSelectionMask_.view : selectionMask_.view;
    mask.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    mask.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    mask.storeOp = multisampled ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
    if (multisampled)
    {
        mask.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        mask.resolveImageView = selectionMask_.view;
        mask.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = depth_.view;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_NONE;
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent = extent_;
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &mask;
    rendering.pDepthAttachment = selected ? &depth : nullptr;
    vkCmdBeginRendering(commandBuffer_, &rendering);
    if (selected)
    {
        vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, selectionPipeline_);
        recordGeometry(std::span{selected, 1}, GeometryPass::Selection);
    }
    vkCmdEndRendering(commandBuffer_);
}

void VulkanRenderer::recordBloom()
{
    const VkViewport viewport{0.0f, 0.0f, float(bloomExtent_.width), float(bloomExtent_.height),
                              0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, bloomExtent_};
    vkCmdSetViewport(commandBuffer_, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer_, 0, 1, &scissor);
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, bloomPipeline_);
    const glm::vec2 texel{1.0f / float(bloomExtent_.width), 1.0f / float(bloomExtent_.height)};
    // XY = blur direction, Z = brightness threshold, W = extract instead of blur.
    const std::array filters{glm::vec4(0, 0, settings.bloomThreshold, 1),
                             glm::vec4(texel.x, 0, 0, 0), glm::vec4(0, texel.y, 0, 0)};
    for (uint32_t pass = 0; pass < filters.size(); ++pass)
    {
        const auto& target = bloomImages_[pass % 2];
        VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.oldLayout =
            pass < 2 ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = target.handle;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(commandBuffer_, &dependency);

        VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        color.imageView = target.view;
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        rendering.renderArea.extent = bloomExtent_;
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;
        vkCmdBeginRendering(commandBuffer_, &rendering);
        vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                bloomPipelineLayout_, 0, 1, &bloomSets_[pass], 0, nullptr);
        vkCmdPushConstants(commandBuffer_, bloomPipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(glm::vec4), &filters[pass]);
        vkCmdDraw(commandBuffer_, 3, 1, 0, 0);
        vkCmdEndRendering(commandBuffer_);

        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        vkCmdPipelineBarrier2(commandBuffer_, &dependency);
    }
}

void VulkanRenderer::recordTransmissionBackground()
{
    const uint32_t mipLevels = std::bit_width(std::max(extent_.width, extent_.height));
    if (!transmissionBackground_.handle)
    {
        transmissionBackground_ =
            createImage(extent_, hdrFormat,
                        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                            VK_IMAGE_USAGE_SAMPLED_BIT,
                        VK_IMAGE_ASPECT_COLOR_BIT, mipLevels);
        writeSampledImage(transmissionSets_[1], 0, transmissionBackground_.view,
                          transmissionSampler_);
    }

    VkImageMemoryBarrier2 source{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    source.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    source.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    source.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    source.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    source.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    source.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    source.srcQueueFamilyIndex = source.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    source.image = hdrColor_.handle;
    source.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    auto target = source;
    target.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    target.srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    target.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    target.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    target.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    target.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    target.image = transmissionBackground_.handle;
    target.subresourceRange.levelCount = mipLevels;
    const std::array copyBarriers{source, target};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = static_cast<uint32_t>(copyBarriers.size());
    dependency.pImageMemoryBarriers = copyBarriers.data();
    vkCmdPipelineBarrier2(commandBuffer_, &dependency);
    const VkImageSubresourceLayers layer{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    const VkImageCopy copy{layer, {}, layer, {}, {extent_.width, extent_.height, 1}};
    vkCmdCopyImage(commandBuffer_, hdrColor_.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   transmissionBackground_.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    generateMipmaps(transmissionBackground_, extent_, 1, mipLevels, commandBuffer_);

    source.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    source.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    source.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    source.dstAccessMask =
        VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    std::swap(source.oldLayout, source.newLayout);
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &source;
    vkCmdPipelineBarrier2(commandBuffer_, &dependency);
}

void VulkanRenderer::record(uint32_t imageIndex, std::span<const RenderInstance> scene,
                            const glm::mat4& view, VkRect2D region,
                            bool directionalShadow, std::optional<ShadowType> localShadow,
                            const std::function<void(VkCommandBuffer)>& overlay)
{
    const auto selection = std::ranges::find_if(scene,
                                                [](const auto& item)
                                                {
                                                    return item.source->selected;
                                                });
    const auto* selected = selection != scene.end() ? &*selection : nullptr;
    const bool hasTransmission = std::ranges::any_of(
        scene,
        [&](const auto& instance)
        {
            const auto& source = *instance.source;
            return std::ranges::any_of(
                models_[source.model.index].draws,
                [&](const Draw& part)
                {
                    return source.nodeVisible(part.node) &&
                           part.usesVariant(source.materialVariant) &&
                           instance.materials[part.materialIndex].advanced.transmission.x > 0.0f;
                });
        });
    check(vkResetCommandBuffer(commandBuffer_, 0), "Reset command buffer");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(commandBuffer_, &begin), "Begin command buffer");

    if (directionalShadow)
        recordShadow(scene, ShadowType::Directional);
    if (localShadow)
        recordShadow(scene, *localShadow);
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    // Discard old HDR contents after the preceding postprocess sampling has finished.
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = hdrColor_.handle;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    // HDR and depth images are shared safely with one frame submission in flight.
    VkImageMemoryBarrier2 depthBarrier = barrier;
    depthBarrier.image = depth_.handle;
    depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthBarrier.srcStageMask =
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    depthBarrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    depthBarrier.dstStageMask = depthBarrier.srcStageMask;
    depthBarrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    VkImageMemoryBarrier2 msaaBarrier = barrier;
    msaaBarrier.image = msaaColor_.handle;
    msaaBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    msaaBarrier.srcAccessMask =
        VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    auto selectionBarrier = barrier;
    selectionBarrier.image = selectionMask_.handle;
    auto msaaSelectionBarrier = msaaBarrier;
    msaaSelectionBarrier.image = msaaSelectionMask_.handle;
    const std::array barriers{barrier, depthBarrier, selectionBarrier, msaaBarrier,
                              msaaSelectionBarrier};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount =
        sceneSamples_ == VK_SAMPLE_COUNT_1_BIT ? 3 : (selected ? 5 : 4);
    dependency.pImageMemoryBarriers = barriers.data();
    vkCmdPipelineBarrier2(commandBuffer_, &dependency);

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = msaaColor_.view ? msaaColor_.view : hdrColor_.view;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    if (sceneSamples_ != VK_SAMPLE_COUNT_1_BIT)
    {
        color.storeOp =
            hasTransmission ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        color.resolveImageView = hdrColor_.view;
        color.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = depth_.view;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = selected || hasTransmission ? VK_ATTACHMENT_STORE_OP_STORE
                                                : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil = {1.0f, 0};
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent = extent_;
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    rendering.pDepthAttachment = &depth;
    vkCmdBeginRendering(commandBuffer_, &rendering);
    const VkViewport viewport{
        0.0f, 0.0f, static_cast<float>(extent_.width), static_cast<float>(extent_.height),
        0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, extent_};
    vkCmdSetViewport(commandBuffer_, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer_, 0, 1, &scissor);
    if (settings.environmentBackground)
    {
        vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline_);
        auto sky = skySet_;
        for (const auto& instance : scene)
        {
            const auto& model = models_[instance.source->model.index];
            if (model.environment.view)
            {
                sky = model.sets.front();
                break;
            }
        }
        vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0,
                                1, &sky, 0, nullptr);
        vkCmdDraw(commandBuffer_, 3, 1, 0, 0);
    }
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 2, 1,
                            &transmissionSets_[0], 0, nullptr);
    recordGeometry(scene, GeometryPass::Opaque);
    if (hasTransmission)
    {
        vkCmdEndRendering(commandBuffer_);
        recordTransmissionBackground();

        // Preserve the per-sample color and depth when reopening the HDR pass.
        VkMemoryBarrier2 resume{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        resume.srcStageMask = resume.dstStageMask =
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        resume.srcAccessMask =
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        resume.dstAccessMask = resume.srcAccessMask | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                               VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        VkDependencyInfo resumeRendering{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        resumeRendering.memoryBarrierCount = 1;
        resumeRendering.pMemoryBarriers = &resume;
        vkCmdPipelineBarrier2(commandBuffer_, &resumeRendering);
        color.loadOp = depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        if (sceneSamples_ != VK_SAMPLE_COUNT_1_BIT)
            color.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        if (!selected)
            depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        vkCmdBeginRendering(commandBuffer_, &rendering);
        vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 2,
                                1, &transmissionSets_[1], 0, nullptr);
        recordGeometry(scene, GeometryPass::Transmission);
    }
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, transparentPipeline_);
    recordGeometry(scene, GeometryPass::Blend, view);
    vkCmdEndRendering(commandBuffer_);

    if (selected)
    {
        depthBarrier.oldLayout = depthBarrier.newLayout;
        depthBarrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &depthBarrier;
        vkCmdPipelineBarrier2(commandBuffer_, &dependency);
    }
    recordSelection(selected);

    // Make the completed HDR scene readable, and acquire the swapchain color attachment.
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    VkImageMemoryBarrier2 swapchainBarrier = barrier;
    swapchainBarrier.image = images_[imageIndex];
    swapchainBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    swapchainBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    swapchainBarrier.srcAccessMask = 0;
    swapchainBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    swapchainBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    selectionBarrier = barrier;
    selectionBarrier.image = selectionMask_.handle;
    const std::array postprocessBarriers{barrier, selectionBarrier, swapchainBarrier};
    dependency.imageMemoryBarrierCount = static_cast<uint32_t>(postprocessBarriers.size());
    dependency.pImageMemoryBarriers = postprocessBarriers.data();
    vkCmdPipelineBarrier2(commandBuffer_, &dependency);

    recordBloom();
    const VkViewport output{float(region.offset.x),
                            float(region.offset.y),
                            float(region.extent.width),
                            float(region.extent.height),
                            0.0f,
                            1.0f};
    vkCmdSetViewport(commandBuffer_, 0, 1, &output);
    vkCmdSetScissor(commandBuffer_, 0, 1, &region);
    color.imageView = views_[imageIndex];
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.resolveMode = VK_RESOLVE_MODE_NONE;
    color.resolveImageView = VK_NULL_HANDLE;
    color.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    rendering.pDepthAttachment = nullptr;
    vkCmdBeginRendering(commandBuffer_, &rendering);
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, postprocessPipeline_);
    vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            postprocessPipelineLayout_, 0, 1, &postprocessSet_, 0, nullptr);
    vkCmdDraw(commandBuffer_, 3, 1, 0, 0);
    if (overlay)
        overlay(commandBuffer_);
    vkCmdEndRendering(commandBuffer_);

    barrier = swapchainBarrier;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    barrier.dstAccessMask = VK_ACCESS_2_NONE;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commandBuffer_, &dependency);
    check(vkEndCommandBuffer(commandBuffer_), "End command buffer");
}

void VulkanRenderer::draw(std::span<const ModelInstance> instances, const glm::mat4& view,
                          const glm::vec3& cameraPosition, float nearPlane, VkRect2D viewport,
                          const std::function<void(VkCommandBuffer)>& overlay)
{
    int width, height;
    glfwGetFramebufferSize(window_, &width, &height);
    while (width == 0 || height == 0 || glfwGetWindowAttrib(window_, GLFW_ICONIFIED))
    {
        if (glfwWindowShouldClose(window_))
            return;
        glfwWaitEvents();
        glfwGetFramebufferSize(window_, &width, &height);
    }
    if (resized_)
        createSwapchain();
    if (glfwWindowShouldClose(window_))
        return;
    if (sceneSamples_ != (settings.msaa ? msaaSamples_ : VK_SAMPLE_COUNT_1_BIT))
    {
        {
            auto lock = lockQueues();
            check(vkDeviceWaitIdle(device_), "Wait before MSAA change");
        }
        createRenderTargets();
    }
    check(vkWaitForFences(device_, 1, &frameFence_, VK_TRUE, UINT64_MAX), "Wait for frame");
    std::vector<RenderInstance> drawInstances;
    drawInstances.reserve(instances.size());
    size_t jointCount = 1; // Zero is reserved for rigid draws.
    size_t morphCount = 1;
    size_t materialCount = 1;
    for (const auto& instance : instances)
    {
        const auto& scene = *instance.model.scene;
        const auto& joints = instance.pose ? instance.pose->joints : scene.restJoints;
        const auto& weights = instance.pose ? instance.pose->morphWeights : scene.restWeights;
        const auto& materials = instance.pose ? instance.pose->materials : scene.restMaterials;
        if (joints.size() > UINT32_MAX - jointCount || weights.size() > UINT32_MAX - morphCount)
            throw std::runtime_error("Too many pose values in the scene");
        if (scene.gpuMaterialCount > (UINT32_MAX >> materialIndexShift) + 1u - materialCount)
            throw std::runtime_error("Too many GPU materials in the scene");
        drawInstances.push_back({&instance, joints.empty() ? 0 : static_cast<uint32_t>(jointCount),
                                 weights.empty() ? 0 : static_cast<uint32_t>(morphCount),
                                 static_cast<uint32_t>(materialCount - 1), joints, weights,
                                 materials});
        jointCount += joints.size();
        morphCount += weights.size();
        materialCount += scene.gpuMaterialCount;
    }
    uploadPose(drawInstances, jointCount, morphCount, materialCount);
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, acquired_,
                                            VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        createSwapchain();
        return;
    }
    const bool suboptimal = result == VK_SUBOPTIMAL_KHR;
    if (!suboptimal)
        check(result, "Acquire swapchain image");
    viewport.offset.x = std::clamp(viewport.offset.x, 0, int32_t(extent_.width - 1));
    viewport.offset.y = std::clamp(viewport.offset.y, 0, int32_t(extent_.height - 1));
    viewport.extent.width =
        std::clamp(viewport.extent.width, 1u, extent_.width - uint32_t(viewport.offset.x));
    viewport.extent.height =
        std::clamp(viewport.extent.height, 1u, extent_.height - uint32_t(viewport.offset.y));
    glm::vec3 boundsMin(std::numeric_limits<float>::max()), boundsMax(-boundsMin);
    for (const auto& instance : instances)
    {
        const auto& transform = instance.transform;
        glm::vec3 localCenter = instance.model.center;
        float localRadius = instance.model.radius;
        if (instance.pose)
        {
            const auto& bounds = instance.pose->bounds;
            localCenter = (bounds.minimum + bounds.maximum) * 0.5f;
            localRadius = glm::length(bounds.maximum - localCenter);
        }
        const auto center = glm::vec3(transform * glm::vec4(localCenter, 1.0f));
        const float scale =
            std::max({glm::length(glm::vec3(transform[0])), glm::length(glm::vec3(transform[1])),
                      glm::length(glm::vec3(transform[2]))});
        const float radius = localRadius * scale;
        boundsMin = glm::min(boundsMin, center - radius);
        boundsMax = glm::max(boundsMax, center + radius);
    }
    const glm::vec3 boundsCenter =
        instances.empty() ? glm::vec3(0.0f) : (boundsMin + boundsMax) * 0.5f;
    const float boundsRadius =
        instances.empty() ? 1.0f : std::max(glm::length(boundsMax - boundsCenter), 0.001f);
    auto projection =
        glm::perspective(glm::radians(verticalFovDegrees),
                         float(viewport.extent.width) / float(viewport.extent.height), nearPlane,
                         glm::distance(cameraPosition, boundsCenter) + 2.0f * boundsRadius + 10.0f);
    projection[1][1] *= -1.0f;
    SceneData scene{projection * view, glm::vec4(cameraPosition, 1.0f)};
    const bool authoredLights = std::ranges::any_of(instances, [](const auto& instance)
    { return !instance.model.scene->lights.empty(); });
    int directionalIndex = -1;
    if (authoredLights)
    {
        size_t count = 0;
        for (const auto& instance : instances)
        {
            for (const auto& light : instance.model.scene->lights)
            {
                if (count == scene.lights.size())
                    break;
                if (!instance.nodeVisible(light.node) || light.intensity == 0.0f)
                    continue;
                const auto world = instance.nodeTransform(light.node);
                glm::vec3 direction(0.0f);
                if (light.type != PunctualLightType::Point)
                {
                    direction = -glm::vec3(world[2]);
                    const float length = glm::length(direction);
                    if (!std::isfinite(length) || length == 0.0f)
                        continue;
                    direction /= length;
                }
                if (light.type == PunctualLightType::Directional && directionalIndex < 0)
                    directionalIndex = static_cast<int>(count);
                scene.lights[count++] = {glm::vec4(glm::vec3(world[3]), light.range),
                                         glm::vec4(light.color, light.intensity),
                                         glm::vec4(direction, static_cast<float>(light.type)),
                                         glm::vec4(light.coneCosines, 0.0f, 0.0f)};
            }
        }
    }
    else
    {
        directionalIndex = 0;
        scene.lights[0] = {glm::vec4(0.0f), glm::vec4(glm::vec3(1.0f), settings.directionalIntensity),
                           glm::vec4(glm::normalize(glm::vec3(-1, -2, -3)),
                                     static_cast<float>(PunctualLightType::Directional)),
                           glm::vec4(0.0f)};
        for (size_t i = 0; i < settings.lights.size(); ++i)
        {
            const auto& light = settings.lights[i];
            const float yaw = glm::radians(light.yaw), pitch = glm::radians(light.pitch);
            const float outer = std::clamp(light.outerAngle, 0.1f, 90.0f);
            const float inner = std::clamp(light.innerAngle, 0.0f, outer - 0.1f);
            scene.lights[i + 1] = {
                glm::vec4(light.position, light.range),
                glm::vec4(light.color, light.enabled && light.range > 0.0f ? light.intensity : 0.0f),
                glm::vec4(glm::cos(pitch) * glm::cos(yaw), glm::sin(pitch),
                          glm::cos(pitch) * glm::sin(yaw), light.spot ? 1.0f : 0.0f),
                glm::vec4(glm::cos(glm::radians(inner)), glm::cos(glm::radians(outer)), 0.01f, 0.0f)};
        }
    }
    scene.lighting = {settings.diffuseIbl ? settings.diffuseIblStrength : 0.0f, settings.exposure,
                      settings.specularIbl ? settings.specularIblStrength : 0.0f,
                      settings.bloom ? settings.bloomStrength : 0.0f};
    scene.inverseSkyViewProjection = glm::inverse(projection * glm::mat4(glm::mat3(view)));
    const bool directionalShadow = settings.shadows && directionalIndex >= 0;
    if (directionalShadow)
    {
        auto lightProjection = glm::ortho(-boundsRadius, boundsRadius, -boundsRadius, boundsRadius,
                                          0.1f * boundsRadius, 4.0f * boundsRadius);
        lightProjection[1][1] *= -1.0f;
        const glm::vec3 direction(scene.lights[directionalIndex].directionType);
        const glm::vec3 up = std::abs(direction.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
        scene.lightViewProjection =
            lightProjection * glm::lookAt(boundsCenter - direction * 2.0f * boundsRadius,
                                         boundsCenter, up);
    }
    scene.shadowParams = {directionalShadow ? float(directionalIndex) : -1.0f, settings.shadowBias,
                          settings.shadowPcf ? 1.0f : 0.0f, 0.0f};
    const int shadowIndex = authoredLights ? -1 : shadowLightIndex();
    scene.localShadowParams = {shadowIndex >= 0 ? float(shadowIndex + 1) : -1.0f,
                               settings.localShadowBias,
                               settings.localPcf ? 1.0f : 0.0f, settings.localFilterRadius};
    std::optional<ShadowType> localShadow;
    if (shadowIndex >= 0)
    {
        const auto& light = settings.lights[shadowIndex];
        const bool spot = light.spot && light.outerAngle <= maxSpotShadowAngle;
        localShadow = spot ? ShadowType::Spot : ShadowType::Point;
        const float far = std::max(light.range, 0.01f), near = std::min(0.05f, far * 0.01f);
        const float angle = std::clamp(light.outerAngle, 0.1f, maxSpotShadowAngle);
        scene.localShadowProjection = {spot ? 1.0f : 0.0f, glm::tan(glm::radians(angle)), 0.0f, 0.0f};
        const auto pointProjection = glm::perspective(glm::radians(90.0f), 1.0f, near, far);
        auto spotProjection = glm::perspective(glm::radians(2.0f * angle), 1.0f, near, far);
        spotProjection[1][1] *= -1.0f;
        const float yaw = glm::radians(light.yaw), pitch = glm::radians(light.pitch);
        // Yaw defines a valid up vector even when pitch is vertical.
        const glm::vec3 spotUp(-glm::sin(pitch) * glm::cos(yaw), glm::cos(pitch),
                               -glm::sin(pitch) * glm::sin(yaw));
        scene.spotViewProjection =
            spotProjection *
            glm::lookAt(light.position,
                        light.position + glm::vec3(scene.lights[shadowIndex + 1].directionType),
                        spotUp);
        // Vulkan cube face order: +X, -X, +Y, -Y, +Z, -Z. Do not flip projection Y.
        const std::array<glm::vec3, 6> directions{
            {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
        const std::array<glm::vec3, 6> up{
            {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}}};
        for (size_t face = 0; face < directions.size(); ++face)
            scene.pointViewProjections[face] =
                pointProjection *
                glm::lookAt(light.position, light.position + directions[face], up[face]);
    }
    // The frame fence has completed; no GPU submission is reading this coherent memory.
    std::memcpy(sceneMapped_, &scene, sizeof(scene));
    {
        std::lock_guard lock(modelsMutex_);
        record(imageIndex, drawInstances, view, viewport, directionalShadow, localShadow, overlay);
    }
    // Reset only after successful acquire, otherwise OUT_OF_DATE would leave an unsignaled fence.
    check(vkResetFences(device_, 1, &frameFence_), "Reset frame fence");
    VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait.semaphore = acquired_;
    wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signal.semaphore = rendered_[imageIndex];
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    command.commandBuffer = commandBuffer_;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &wait;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signal;
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &rendered_[imageIndex];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &imageIndex;
    {
        auto lock = lockQueues();
        check(vkQueueSubmit2(graphicsQueue_, 1, &submit, frameFence_), "Submit frame");
        result = vkQueuePresentKHR(presentQueue_, &present);
    }
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || suboptimal)
    {
        // Defer recreation until drawable dimensions are available on the next frame.
        resized_ = true;
        if (result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_SUBOPTIMAL_KHR)
            check(result, "Present frame");
    }
    else
        check(result, "Present frame");
}

void VulkanRenderer::destroyRenderTargets()
{
    destroyImage(depth_);
    destroyImage(hdrColor_);
    destroyImage(transmissionBackground_);
    destroyImage(msaaColor_);
    destroyImage(selectionMask_);
    destroyImage(msaaSelectionMask_);
    for (auto& image : bloomImages_)
        destroyImage(image);
}

void VulkanRenderer::destroySwapchain()
{
    destroyRenderTargets();
    for (auto semaphore : rendered_)
        vkDestroySemaphore(device_, semaphore, nullptr);
    for (auto view : views_)
        vkDestroyImageView(device_, view, nullptr);
    rendered_.clear();
    views_.clear();
    images_.clear();
    vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
}

void VulkanRenderer::destroy()
{
    if (device_)
    {
        vkDeviceWaitIdle(device_);
        for (auto& model : models_)
            destroyModel(model);
        vkDestroyPipeline(device_, pipeline_, nullptr);
        vkDestroyPipeline(device_, transparentPipeline_, nullptr);
        vkDestroyPipeline(device_, shadowPipeline_, nullptr);
        vkDestroyPipeline(device_, selectionPipeline_, nullptr);
        vkDestroyPipeline(device_, skyPipeline_, nullptr);
        vkDestroyPipeline(device_, bloomPipeline_, nullptr);
        vkDestroyPipeline(device_, postprocessPipeline_, nullptr);
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        vkDestroyPipelineLayout(device_, bloomPipelineLayout_, nullptr);
        vkDestroyPipelineLayout(device_, postprocessPipelineLayout_, nullptr);
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        vkDestroyDescriptorSetLayout(device_, descriptorLayout_, nullptr);
        vkDestroyDescriptorSetLayout(device_, jointLayout_, nullptr);
        vkDestroyDescriptorSetLayout(device_, bloomDescriptorLayout_, nullptr);
        vkDestroyDescriptorSetLayout(device_, postprocessDescriptorLayout_, nullptr);
        if (sceneMapped_)
            vkUnmapMemory(device_, sceneBuffer_.memory);
        sceneMapped_ = nullptr;
        destroyBuffer(sceneBuffer_);
        destroyBuffer(jointBuffer_);
        destroyBuffer(morphWeightBuffer_);
        destroyBuffer(materialBuffer_);
        destroyBuffer(emptyGeometryBuffer_);
        vkDestroySampler(device_, sampler_, nullptr);
        vkDestroySampler(device_, shadowSampler_, nullptr);
        destroyImage(shadowMap_);
        for (auto view : pointShadowFaces_)
            vkDestroyImageView(device_, view, nullptr);
        destroyImage(pointShadowMap_);
        destroyImage(spotShadowMap_);
        vkDestroySampler(device_, environmentSampler_, nullptr);
        vkDestroySampler(device_, clampSampler_, nullptr);
        vkDestroySampler(device_, transmissionSampler_, nullptr);
        destroyImage(environment_);
        destroyImage(irradiance_);
        destroyImage(prefilteredEnvironment_);
        destroyImage(prefilteredSheen_);
        destroyImage(brdfLut_);
        destroySwapchain();
        vkDestroyFence(device_, frameFence_, nullptr);
        vkDestroySemaphore(device_, acquired_, nullptr);
        vkDestroyCommandPool(device_, commandPool_, nullptr);
        vkDestroyDevice(device_, nullptr);
    }
    if (surface_)
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (messenger_)
    {
        auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        destroyMessenger(instance_, messenger_, nullptr);
    }
    if (instance_)
        vkDestroyInstance(instance_, nullptr);
}
