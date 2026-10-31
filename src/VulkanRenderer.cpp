#include "VulkanRenderer.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <iostream>
#include <memory>
#include <stb_image.h>
#include <stdexcept>
#include <string>

namespace
{
struct Vertex
{
    glm::vec3 position;
    glm::vec2 uv;
    glm::vec3 normal;
};

struct PushConstants
{
    glm::mat4 mvp;
    glm::mat4 model;
};
static_assert(sizeof(PushConstants) == 128 && offsetof(PushConstants, model) == 64);

// Each face has its own vertices for independent UVs and flat normals.
const std::array<Vertex, 24> vertices{{
    // -Z
    {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
    {{-0.5f, 0.5f, -0.5f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
    {{0.5f, 0.5f, -0.5f}, {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
    {{0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
    // +Z
    {{-0.5f, -0.5f, 0.5f}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
    {{0.5f, -0.5f, 0.5f}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
    {{0.5f, 0.5f, 0.5f}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    {{-0.5f, 0.5f, 0.5f}, {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    // -X
    {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}},
    {{-0.5f, -0.5f, 0.5f}, {1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}},
    {{-0.5f, 0.5f, 0.5f}, {1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}},
    {{-0.5f, 0.5f, -0.5f}, {0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}},
    // +X
    {{0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}},
    {{0.5f, 0.5f, -0.5f}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}},
    {{0.5f, 0.5f, 0.5f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
    {{0.5f, -0.5f, 0.5f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
    // -Y
    {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}, {0.0f, -1.0f, 0.0f}},
    {{0.5f, -0.5f, -0.5f}, {1.0f, 1.0f}, {0.0f, -1.0f, 0.0f}},
    {{0.5f, -0.5f, 0.5f}, {1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}},
    {{-0.5f, -0.5f, 0.5f}, {0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}},
    // +Y
    {{-0.5f, 0.5f, -0.5f}, {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
    {{-0.5f, 0.5f, 0.5f}, {1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
    {{0.5f, 0.5f, 0.5f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
}};
constexpr std::array<uint16_t, 36> indices{
    0,  1,  2,  2,  3,  0,  4,  5,  6,  6,  7,  4,  8,  9,  10, 10, 11, 8,
    12, 13, 14, 14, 15, 12, 16, 17, 18, 18, 19, 16, 20, 21, 22, 22, 23, 20,
};

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

        vertexBuffer_ =
            uploadBuffer(vertices.data(), sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        indexBuffer_ =
            uploadBuffer(indices.data(), sizeof(indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        createTexture();
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
        if (properties.apiVersion < VK_API_VERSION_1_3 || !features13.dynamicRendering ||
            !features13.synchronization2)
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
            if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                graphics = i;
            if (supported)
                present = i;
            if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && supported)
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
        throw std::runtime_error(
            "No GPU supports Vulkan 1.3, dynamic rendering, synchronization2 and presentation");

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
                                                    VkBufferUsageFlags usage)
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

        beginUpload();
        const VkBufferCopy region{0, 0, size};
        vkCmdCopyBuffer(commandBuffer_, staging.handle, destination.handle, 1, &region);
        VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.memoryBarrierCount = 1;
        dependency.pMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(commandBuffer_, &dependency);
        endUpload();
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

void VulkanRenderer::beginUpload()
{
    // Initialization only: reuse the frame command buffer before any frame is in flight.
    check(vkResetCommandBuffer(commandBuffer_, 0), "Reset upload command buffer");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(commandBuffer_, &begin), "Begin upload");
}

void VulkanRenderer::endUpload()
{
    check(vkEndCommandBuffer(commandBuffer_), "End upload");
    VkCommandBufferSubmitInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    command.commandBuffer = commandBuffer_;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &command;
    check(vkQueueSubmit2(graphicsQueue_, 1, &submit, VK_NULL_HANDLE), "Submit upload");
    check(vkQueueWaitIdle(graphicsQueue_), "Wait before releasing staging resources");
}

VulkanRenderer::Image VulkanRenderer::createImage(VkExtent2D extent, VkFormat format,
                                                  VkImageUsageFlags usage,
                                                  VkImageAspectFlags aspect, uint32_t mipLevels)
{
    Image image;
    try
    {
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {extent.width, extent.height, 1};
        info.mipLevels = mipLevels;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
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
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange = {aspect, 0, mipLevels, 0, 1};
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

void VulkanRenderer::createTexture()
{
    const auto path = std::filesystem::path(KOSMOS_ASSET_DIR) / "textures/stone.png";
    int width = 0, height = 0;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load(path.string().c_str(), &width, &height, nullptr, STBI_rgb_alpha),
        stbi_image_free);
    if (!pixels)
        throw std::runtime_error("Cannot load texture: " + path.string() + " (" +
                                 stbi_failure_reason() + ")");
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(physicalDevice_, &deviceProperties);
    if (width <= 0 || height <= 0 ||
        static_cast<uint32_t>(width) > deviceProperties.limits.maxImageDimension2D ||
        static_cast<uint32_t>(height) > deviceProperties.limits.maxImageDimension2D)
        throw std::runtime_error("Texture dimensions exceed device limits: " + path.string());
    const VkExtent2D extent{static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * height * STBI_rgb_alpha;
    const uint32_t mipLevels =
        static_cast<uint32_t>(std::bit_width(std::max(extent.width, extent.height)));
    constexpr VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
    VkFormatProperties properties;
    vkGetPhysicalDeviceFormatProperties(physicalDevice_, format, &properties);
    constexpr auto required =
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
        VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
    if ((properties.optimalTilingFeatures & required) != required)
        throw std::runtime_error("RGBA8 sRGB linear blit and sampling are unsupported");

    Buffer staging;
    try
    {
        staging = createBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void* mapped = nullptr;
        check(vkMapMemory(device_, staging.memory, 0, bytes, 0, &mapped),
              "Map texture staging memory");
        std::memcpy(mapped, pixels.get(), static_cast<size_t>(bytes));
        pixels.reset();
        vkUnmapMemory(device_, staging.memory);
        texture_ = createImage(extent, format,
                               VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                   VK_IMAGE_USAGE_SAMPLED_BIT,
                               VK_IMAGE_ASPECT_COLOR_BIT, mipLevels);
        beginUpload();
        VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = texture_.handle;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1};
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(commandBuffer_, &dependency);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {extent.width, extent.height, 1};
        vkCmdCopyBufferToImage(commandBuffer_, staging.handle, texture_.handle,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        generateMipmaps(extent, mipLevels);
        endUpload();
    }
    catch (...)
    {
        destroyBuffer(staging);
        throw;
    }
    destroyBuffer(staging);
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.maxLod = static_cast<float>(mipLevels - 1);
    check(vkCreateSampler(device_, &sampler, nullptr, &sampler_), "Create sampler");
    std::cout << "Texture: " << width << 'x' << height << ", " << mipLevels << " mip levels\n";
}

void VulkanRenderer::generateMipmaps(VkExtent2D extent, uint32_t mipLevels)
{
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture_.handle;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    int32_t width = static_cast<int32_t>(extent.width),
            height = static_cast<int32_t>(extent.height);
    for (uint32_t level = 1; level < mipLevels; ++level)
    {
        // The previous level was written by the initial copy or the preceding blit.
        barrier.subresourceRange.baseMipLevel = level - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier2(commandBuffer_, &dependency);
        const int32_t nextWidth = std::max(width / 2, 1), nextHeight = std::max(height / 2, 1);
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 1};
        blit.srcOffsets[1] = {width, height, 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
        blit.dstOffsets[1] = {nextWidth, nextHeight, 1};
        vkCmdBlitImage(commandBuffer_, texture_.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       texture_.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                       VK_FILTER_LINEAR);
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_BLIT_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        vkCmdPipelineBarrier2(commandBuffer_, &dependency);
        width = nextWidth;
        height = nextHeight;
    }
    // The last level remains a transfer destination (also covers a 1x1 source image).
    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    vkCmdPipelineBarrier2(commandBuffer_, &dependency);
}

void VulkanRenderer::createDescriptors()
{
    const VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                               VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout.bindingCount = 1;
    layout.pBindings = &binding;
    check(vkCreateDescriptorSetLayout(device_, &layout, nullptr, &descriptorLayout_),
          "Create descriptor layout");
    const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = 1;
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &size;
    check(vkCreateDescriptorPool(device_, &pool, nullptr, &descriptorPool_),
          "Create descriptor pool");
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = descriptorPool_;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &descriptorLayout_;
    check(vkAllocateDescriptorSets(device_, &allocate, &descriptorSet_), "Allocate descriptor set");
    const VkDescriptorImageInfo image{sampler_, texture_.view,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = descriptorSet_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &image;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
}

void VulkanRenderer::createPipeline()
{
    if (!pipelineLayout_)
    {
        const VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants)};
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &descriptorLayout_;
        layout.pushConstantRangeCount = 1;
        layout.pPushConstantRanges = &push;
        check(vkCreatePipelineLayout(device_, &layout, nullptr, &pipelineLayout_),
              "Create pipeline layout");
    }

    std::array<VkShaderModule, 2> modules{};
    try
    {
        modules[0] = loadShader(device_, "mesh.vertex.spv");
        modules[1] = loadShader(device_, "mesh.fragment.spv");
        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        for (auto& stage : stages)
            stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = modules[0];
        stages[0].pName = "vertexMain";
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = modules[1];
        stages[1].pName = "fragmentMain";

        const VkVertexInputBindingDescription binding{0, sizeof(Vertex),
                                                      VK_VERTEX_INPUT_RATE_VERTEX};
        const std::array<VkVertexInputAttributeDescription, 3> attributes{{
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
            {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
            {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
        }};
        VkPipelineVertexInputStateCreateInfo vertex{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertex.vertexBindingDescriptionCount = 1;
        vertex.pVertexBindingDescriptions = &binding;
        vertex.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
        vertex.pVertexAttributeDescriptions = attributes.data();
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
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS;
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 1;
        blend.pAttachments = &attachment;
        const std::array states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = static_cast<uint32_t>(states.size());
        dynamic.pDynamicStates = states.data();
        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachmentFormats = &format_;
        rendering.depthAttachmentFormat = depthFormat_;
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
        info.layout = pipelineLayout_;
        check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline_),
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

void VulkanRenderer::createDepthResources()
{
    if (!depthFormat_)
    {
        for (auto format : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM})
        {
            VkFormatProperties properties;
            vkGetPhysicalDeviceFormatProperties(physicalDevice_, format, &properties);
            if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            {
                depthFormat_ = format;
                break;
            }
        }
        if (!depthFormat_)
            throw std::runtime_error("No supported depth attachment format");
    }
    depth_ = createImage(extent_, depthFormat_, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                         VK_IMAGE_ASPECT_DEPTH_BIT);
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
    // Only resize/shutdown wait for the whole device; ordinary frames use a fence.
    check(vkDeviceWaitIdle(device_), "Wait before swapchain recreation");
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
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            selected = format;
    if (selected.format == VK_FORMAT_UNDEFINED)
        selected.format = VK_FORMAT_B8G8R8A8_SRGB;
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
    createDepthResources();
    if (!pipeline_ || formatChanged)
    {
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
        createPipeline();
    }
    resized_ = false;
}

void VulkanRenderer::record(uint32_t imageIndex, const glm::mat4& view)
{
    check(vkResetCommandBuffer(commandBuffer_, 0), "Reset command buffer");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(commandBuffer_, &begin), "Begin command buffer");

    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    // Clear every frame, so previous contents may be discarded even after presentation.
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    // Chain the acquire semaphore wait to the layout transition.
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = images_[imageIndex];
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    // One depth image is shared safely because only one frame submission is in flight.
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
    const std::array barriers{barrier, depthBarrier};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
    dependency.pImageMemoryBarriers = barriers.data();
    vkCmdPipelineBarrier2(commandBuffer_, &dependency);

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = views_[imageIndex];
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{0.02f, 0.04f, 0.08f, 1.0f}};
    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = depth_.view;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil = {1.0f, 0};
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent = extent_;
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    rendering.pDepthAttachment = &depth;
    vkCmdBeginRendering(commandBuffer_, &rendering);
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1,
                            &descriptorSet_, 0, nullptr);
    const VkViewport viewport{
        0.0f, 0.0f, static_cast<float>(extent_.width), static_cast<float>(extent_.height),
        0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, extent_};
    vkCmdSetViewport(commandBuffer_, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer_, 0, 1, &scissor);
    const float angle = static_cast<float>(glfwGetTime()) * glm::radians(45.0f);
    const auto model =
        glm::rotate(glm::mat4(1.0f), angle, glm::normalize(glm::vec3(0.5f, 1.0f, 0.0f)));
    auto projection = glm::perspective(
        glm::radians(45.0f), static_cast<float>(extent_.width) / static_cast<float>(extent_.height),
        0.1f, 10.0f);
    projection[1][1] *= -1.0f;
    const PushConstants constants{projection * view * model, model};
    vkCmdPushConstants(commandBuffer_, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(constants), &constants);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer_, 0, 1, &vertexBuffer_.handle, &offset);
    vkCmdBindIndexBuffer(commandBuffer_, indexBuffer_.handle, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(commandBuffer_, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
    vkCmdEndRendering(commandBuffer_);

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

void VulkanRenderer::draw(const glm::mat4& view)
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
    check(vkWaitForFences(device_, 1, &frameFence_, VK_TRUE, UINT64_MAX), "Wait for frame");
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
    record(imageIndex, view);
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
    check(vkQueueSubmit2(graphicsQueue_, 1, &submit, frameFence_), "Submit frame");
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &rendered_[imageIndex];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &imageIndex;
    result = vkQueuePresentKHR(presentQueue_, &present);
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

void VulkanRenderer::destroySwapchain()
{
    destroyImage(depth_);
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
        destroyBuffer(indexBuffer_);
        destroyBuffer(vertexBuffer_);
        vkDestroyPipeline(device_, pipeline_, nullptr);
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        vkDestroyDescriptorSetLayout(device_, descriptorLayout_, nullptr);
        vkDestroySampler(device_, sampler_, nullptr);
        destroyImage(texture_);
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
