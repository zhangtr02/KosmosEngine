#include "VulkanRenderer.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cfloat>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <iostream>
#include <memory>
#include <stb_image.h>
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

struct PushConstants
{
    glm::mat4 model;
    glm::vec4 baseColorFactor;
    glm::vec4 materialParams; // Normal scale, metallic factor, roughness factor, AO strength.
    glm::vec4 emissiveFactor; // Linear RGB, including material strength and the UI multiplier.
    glm::vec4
        surfaceParams; // Alpha cutoff, BLEND enabled, point shadow face (-1 otherwise), unused.
};
static_assert(sizeof(PushConstants) == 128 && offsetof(PushConstants, baseColorFactor) == 64 &&
              offsetof(PushConstants, materialParams) == 80 &&
              offsetof(PushConstants, emissiveFactor) == 96 &&
              offsetof(PushConstants, surfaceParams) == 112);

struct SceneData
{
    glm::mat4 viewProjection;
    glm::vec4 cameraPosition;
    glm::vec4 lightDirection{1.0f, 2.0f, 3.0f, 0.0f}; // Surface to light, world space.
    glm::vec4 lightColor{3.0f, 3.0f, 3.0f, 0.0f};
    glm::vec4 lighting{}; // Diffuse IBL, exposure, specular IBL, Bloom; set from RenderSettings.
    glm::mat4 inverseSkyViewProjection;
    glm::mat4 lightViewProjection;
    glm::vec4 shadowParams; // Enabled, depth bias, PCF enabled, unused.
    glm::vec4 pointPositionRange;
    glm::vec4 pointColorIntensity;
    std::array<glm::mat4, 6> pointViewProjections;
    glm::vec4 pointShadowParams; // Enabled, world-space bias, PCF enabled, filter radius in texels.
};
static_assert(sizeof(SceneData) == 704 && offsetof(SceneData, cameraPosition) == 64 &&
              offsetof(SceneData, lightDirection) == 80 && offsetof(SceneData, lightColor) == 96 &&
              offsetof(SceneData, lighting) == 112 &&
              offsetof(SceneData, inverseSkyViewProjection) == 128 &&
              offsetof(SceneData, lightViewProjection) == 192 &&
              offsetof(SceneData, shadowParams) == 256 &&
              offsetof(SceneData, pointPositionRange) == 272 &&
              offsetof(SceneData, pointColorIntensity) == 288 &&
              offsetof(SceneData, pointViewProjections) == 304 &&
              offsetof(SceneData, pointShadowParams) == 688);

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

        auto model =
            loadModel(std::filesystem::path(KOSMOS_ASSET_DIR) / "models/BoomBox/BoomBox.gltf");
        draws_ = model.draws;
        const auto whiteTexture = [&](bool srgb)
        {
            const TextureSource source{{}, srgb};
            const auto found = std::find(model.textures.begin(), model.textures.end(), source);
            const auto index = static_cast<uint32_t>(found - model.textures.begin());
            if (found == model.textures.end())
                model.textures.push_back(source);
            return index;
        };
        Material ground;
        ground.baseColorFactor = glm::vec4(0.35f, 0.35f, 0.35f, 1.0f);
        ground.metallicFactor = 0.0f;
        ground.textureIndex = whiteTexture(true);
        ground.emissiveTextureIndex = ground.textureIndex;
        ground.normalTextureIndex = ground.metallicRoughnessTextureIndex =
            ground.occlusionTextureIndex = whiteTexture(false);
        groundDraw_ = {static_cast<uint32_t>(model.indices.size()), 6,
                       static_cast<int32_t>(model.vertices.size()),
                       static_cast<uint32_t>(model.materials.size())};
        model.materials.push_back(ground);
        const float y = std::min_element(model.vertices.begin(), model.vertices.end(),
                                         [](const Vertex& a, const Vertex& b)
                                         {
                                             return a.position.y < b.position.y;
                                         })
                            ->position.y -
                        0.01f;
        const std::array<Vertex, 4> vertices{{
            {{-3, y, -3}, {0, 0}, {0, 1, 0}},
            {{3, y, -3}, {1, 0}, {0, 1, 0}},
            {{3, y, 3}, {1, 1}, {0, 1, 0}},
            {{-3, y, 3}, {0, 1}, {0, 1, 0}},
        }};
        model.vertices.insert(model.vertices.end(), vertices.begin(), vertices.end());
        model.indices.insert(model.indices.end(), {0, 2, 1, 0, 3, 2});
        materials_ = model.materials;
        vertexBuffer_ = uploadBuffer(model.vertices.data(), model.vertices.size() * sizeof(Vertex),
                                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        indexBuffer_ = uploadBuffer(model.indices.data(), model.indices.size() * sizeof(uint32_t),
                                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        textures_.resize(model.textures.size());
        for (size_t i = 0; i < textures_.size(); ++i)
            textures_[i] = createTexture(model.textures[i]);
        std::cout << "Scene: " << model.vertices.size() << " vertices, " << model.indices.size()
                  << " indices, " << draws_.size() + 1 << " draws, " << materials_.size()
                  << " materials, " << textures_.size() << " textures\n";
        createEnvironment();
        irradiance_ = precomputeEnvironment("irradiance.compute.spv", {64, 32});
        // Stop at 16x8: even rough reflections retain directional lighting.
        prefilteredEnvironment_ = precomputeEnvironment("prefilter.compute.spv", {256, 128}, 5);
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
        createUI();
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

void VulkanRenderer::createUI()
{
    IMGUI_CHECKVERSION();
    uiContext_ = ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    if (!ImGui_ImplGlfw_InitForVulkan(window_, true))
        throw std::runtime_error("Initialize ImGui GLFW backend");
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = instance_;
    info.PhysicalDevice = physicalDevice_;
    info.Device = device_;
    info.QueueFamily = graphicsFamily_;
    info.Queue = graphicsQueue_;
    info.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE;
    info.MinImageCount = 2;
    info.ImageCount = static_cast<uint32_t>(images_.size());
    info.UseDynamicRendering = true;
    auto& rendering = info.PipelineInfoMain.PipelineRenderingCreateInfo;
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &format_;
    info.CheckVkResultFn = [](VkResult result)
    {
        check(result, "ImGui Vulkan");
    };
    if (!ImGui_ImplVulkan_Init(&info))
        throw std::runtime_error("Initialize ImGui Vulkan backend");
}

void VulkanRenderer::updateUI()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(0, 0), ImVec2(FLT_MAX, std::max(ImGui::GetIO().DisplaySize.y - 24.0f, 1.0f)));
    if (ImGui::Begin("Rendering", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        const float fps = ImGui::GetIO().Framerate;
        ImGui::Text("%.1f FPS / %.2f ms", fps, 1000.0f / std::max(fps, 1.0f));
        ImGui::SliderFloat("Exposure", &settings.exposure, 0.05f, 8.0f, "%.2f",
                           ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
        ImGui::Checkbox("MSAA", &settings.msaa);
        ImGui::SameLine();
        ImGui::TextDisabled("(%ux)",
                            uint32_t(settings.msaa ? msaaSamples_ : VK_SAMPLE_COUNT_1_BIT));
        if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SliderFloat("Sun intensity", &settings.directionalIntensity, 0.0f, 10.0f, "%.2f",
                               ImGuiSliderFlags_AlwaysClamp);
            ImGui::Checkbox("Point light", &settings.pointLight);
            ImGui::BeginDisabled(!settings.pointLight);
            ImGui::SliderFloat3("Position", &settings.pointPosition.x, -5.0f, 5.0f, "%.2f",
                                ImGuiSliderFlags_AlwaysClamp);
            ImGui::ColorEdit3("Color", &settings.pointColor.x);
            ImGui::SliderFloat("Intensity", &settings.pointIntensity, 0.0f, 100.0f, "%.2f",
                               ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("Range", &settings.pointRange, 0.1f, 10.0f, "%.2f",
                               ImGuiSliderFlags_AlwaysClamp);
            ImGui::Checkbox("Point shadows", &settings.pointShadows);
            ImGui::BeginDisabled(!settings.pointShadows);
            ImGui::SliderFloat("Point bias", &settings.pointShadowBias, 0.0f, 0.05f, "%.4f",
                               ImGuiSliderFlags_AlwaysClamp);
            ImGui::Checkbox("Point PCF", &settings.pointPcf);
            ImGui::BeginDisabled(!settings.pointPcf);
            ImGui::SliderFloat("Point filter radius", &settings.pointFilterRadius, 0.0f, 4.0f,
                               "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            ImGui::EndDisabled();
        }
        ImGui::Separator();
        ImGui::Checkbox("Bloom", &settings.bloom);
        ImGui::BeginDisabled(!settings.bloom);
        ImGui::SliderFloat("Strength", &settings.bloomStrength, 0.0f, 1.0f, "%.3f",
                           ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Threshold", &settings.bloomThreshold, 0.0f, 10.0f, "%.3f",
                           ImGuiSliderFlags_AlwaysClamp);
        ImGui::EndDisabled();
        ImGui::Checkbox("Emissive", &settings.emissive);
        ImGui::BeginDisabled(!settings.emissive);
        ImGui::SliderFloat("Emissive multiplier", &settings.emissiveStrength, 0.0f, 20.0f, "%.2f",
                           ImGuiSliderFlags_AlwaysClamp);
        ImGui::EndDisabled();
        ImGui::Checkbox("Diffuse IBL", &settings.diffuseIbl);
        ImGui::BeginDisabled(!settings.diffuseIbl);
        ImGui::SliderFloat("Diffuse strength", &settings.diffuseIblStrength, 0.0f, 3.0f, "%.3f",
                           ImGuiSliderFlags_AlwaysClamp);
        ImGui::EndDisabled();
        ImGui::Checkbox("Specular IBL", &settings.specularIbl);
        ImGui::BeginDisabled(!settings.specularIbl);
        ImGui::SliderFloat("Specular strength", &settings.specularIblStrength, 0.0f, 3.0f, "%.3f",
                           ImGuiSliderFlags_AlwaysClamp);
        ImGui::EndDisabled();
        ImGui::Checkbox("Ambient occlusion", &settings.ambientOcclusion);
        ImGui::Checkbox("Shadows", &settings.shadows);
        ImGui::BeginDisabled(!settings.shadows);
        ImGui::SliderFloat("Depth bias", &settings.shadowBias, 0.0f, 0.005f, "%.5f",
                           ImGuiSliderFlags_AlwaysClamp);
        ImGui::Checkbox("Shadow PCF", &settings.shadowPcf);
        ImGui::EndDisabled();
        ImGui::Separator();
        ImGui::Checkbox("Rotate model", &settings.rotateModel);
        if (ImGui::Button("Reset settings"))
            settings = {};
        ImGui::TextDisabled("WASD: move / Right mouse: look");
    }
    ImGui::End();
    ImGui::Render();
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
                                 "synchronization2, shader demotion and presentation");

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

VulkanRenderer::Image VulkanRenderer::createTexture(const TextureSource& source)
{
    const auto& path = source.path;
    int width = 1, height = 1;
    const auto fallback = source.flatNormal ? std::array<stbi_uc, 4>{128, 128, 255, 255}
                                            : std::array<stbi_uc, 4>{255, 255, 255, 255};
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(nullptr, stbi_image_free);
    if (!path.empty())
    {
        pixels.reset(stbi_load(path.string().c_str(), &width, &height, nullptr, STBI_rgb_alpha));
        if (!pixels)
            throw std::runtime_error("Cannot load texture: " + path.string() + " (" +
                                     stbi_failure_reason() + ")");
    }
    const VkExtent2D extent{static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * height * STBI_rgb_alpha;
    const uint32_t mipLevels =
        static_cast<uint32_t>(std::bit_width(std::max(extent.width, extent.height)));
    const VkFormat format = source.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    auto image =
        uploadImage(pixels ? pixels.get() : fallback.data(), extent, format, bytes, mipLevels);
    std::cout << "Texture: " << width << 'x' << height << ", " << mipLevels << " mip levels\n";
    return image;
}

VulkanRenderer::Image VulkanRenderer::uploadImage(const void* pixels, VkExtent2D extent,
                                                  VkFormat format, VkDeviceSize bytes,
                                                  uint32_t mipLevels)
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
    if (mipLevels > 1)
        required |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT |
                    VK_FORMAT_FEATURE_BLIT_DST_BIT;
    if ((properties.optimalTilingFeatures & required) != required)
        throw std::runtime_error("Texture format does not support sampling and linear blits");

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
                                        (mipLevels > 1 ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
        image = createImage(extent, format, usage, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels);
        beginUpload();
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
        vkCmdPipelineBarrier2(commandBuffer_, &dependency);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {extent.width, extent.height, 1};
        vkCmdCopyBufferToImage(commandBuffer_, staging.handle, image.handle,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        generateMipmaps(image, extent, mipLevels);
        endUpload();
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
    // Poly Haven: https://polyhaven.com/a/spruit_sunrise (CC0).
    const auto path =
        std::filesystem::path(KOSMOS_ASSET_DIR) / "environments/spruit_sunrise_1k.hdr";
    if (!stbi_is_hdr(path.string().c_str()))
        throw std::runtime_error("Radiance HDR environment required: " + path.string());
    int width = 0, height = 0;
    std::unique_ptr<float, decltype(&stbi_image_free)> pixels(
        stbi_loadf(path.string().c_str(), &width, &height, nullptr, STBI_rgb_alpha),
        stbi_image_free);
    if (!pixels)
        throw std::runtime_error("Cannot load environment: " + path.string() + " (" +
                                 stbi_failure_reason() + ")");
    if (width <= 0 || height <= 0 || int64_t(width) != int64_t(height) * 2)
        throw std::runtime_error("Environment must be a 2:1 equirectangular image");
    environment_ =
        uploadImage(pixels.get(), {uint32_t(width), uint32_t(height)},
                    VK_FORMAT_R32G32B32A32_SFLOAT, VkDeviceSize(width) * height * 4 * sizeof(float),
                    static_cast<uint32_t>(std::bit_width(uint32_t(width))));
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.maxLod = VK_LOD_CLAMP_NONE;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    check(vkCreateSampler(device_, &sampler, nullptr, &environmentSampler_),
          "Create environment sampler");
    std::cout << "Environment: " << width << 'x' << height << " HDR\n";
}

VulkanRenderer::Image VulkanRenderer::precomputeEnvironment(const char* shaderFilename,
                                                            VkExtent2D extent, uint32_t mipLevels)
{
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
        const VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(float)};
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

        beginUpload();
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
            const float roughness = mipLevels > 1 ? float(level) / float(mipLevels - 1) : 0.0f;
            vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout,
                                    0, 1, &descriptors[level], 0, nullptr);
            vkCmdPushConstants(commandBuffer_, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                               sizeof(roughness), &roughness);
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
        endUpload();
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

void VulkanRenderer::generateMipmaps(const Image& image, VkExtent2D extent, uint32_t mipLevels)
{
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image.handle;
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
        vkCmdBlitImage(commandBuffer_, image.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       image.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
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
    shadowMap_ =
        createImage(shadowExtent, depthFormat_,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_DEPTH_BIT);
    pointShadowMap_ =
        createImage(pointShadowExtent, depthFormat_,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_DEPTH_BIT, 1, VK_SAMPLE_COUNT_1_BIT, true);
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
    sceneBuffer_ =
        createBuffer(sizeof(SceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkMapMemory(device_, sceneBuffer_.memory, 0, sizeof(SceneData), 0, &sceneMapped_),
          "Map scene uniform buffer");
    const std::array<VkDescriptorSetLayoutBinding, 12> bindings{{
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
    }};
    VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout.bindingCount = static_cast<uint32_t>(bindings.size());
    layout.pBindings = bindings.data();
    check(vkCreateDescriptorSetLayout(device_, &layout, nullptr, &descriptorLayout_),
          "Create descriptor layout");
    const std::array postprocessBindings{bindings[0], bindings[1], bindings[2]};
    layout.bindingCount = static_cast<uint32_t>(postprocessBindings.size());
    layout.pBindings = postprocessBindings.data();
    check(vkCreateDescriptorSetLayout(device_, &layout, nullptr, &postprocessDescriptorLayout_),
          "Create postprocess descriptor layout");
    layout.bindingCount = 1;
    layout.pBindings = bindings.data();
    check(vkCreateDescriptorSetLayout(device_, &layout, nullptr, &bloomDescriptorLayout_),
          "Create Bloom descriptor layout");
    const auto count = static_cast<uint32_t>(materials_.size());
    const std::array<VkDescriptorPoolSize, 2> sizes{{
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, count * 11 + 5},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, count + 1},
    }};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = count + 4;
    pool.poolSizeCount = static_cast<uint32_t>(sizes.size());
    pool.pPoolSizes = sizes.data();
    check(vkCreateDescriptorPool(device_, &pool, nullptr, &descriptorPool_),
          "Create descriptor pool");
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = descriptorPool_;
    allocate.descriptorSetCount = count;
    const std::vector<VkDescriptorSetLayout> layouts(count, descriptorLayout_);
    allocate.pSetLayouts = layouts.data();
    descriptorSets_.resize(count);
    check(vkAllocateDescriptorSets(device_, &allocate, descriptorSets_.data()),
          "Allocate descriptor sets");
    const VkDescriptorBufferInfo buffer{sceneBuffer_.handle, 0, sizeof(SceneData)};
    const VkDescriptorImageInfo environment{environmentSampler_, environment_.view,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo irradiance{environmentSampler_, irradiance_.view,
                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo prefiltered{environmentSampler_, prefilteredEnvironment_.view,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo brdf{clampSampler_, brdfLut_.view,
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo shadow{shadowSampler_, shadowMap_.view,
                                       VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo pointShadow{shadowSampler_, pointShadowMap_.view,
                                            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    for (size_t i = 0; i < materials_.size(); ++i)
    {
        const auto& material = materials_[i];
        const VkDescriptorImageInfo image{sampler_, textures_[material.textureIndex].view,
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        const VkDescriptorImageInfo normal{sampler_, textures_[material.normalTextureIndex].view,
                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        const VkDescriptorImageInfo metallicRoughness{
            sampler_, textures_[material.metallicRoughnessTextureIndex].view,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        const VkDescriptorImageInfo occlusion{sampler_,
                                              textures_[material.occlusionTextureIndex].view,
                                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        const VkDescriptorImageInfo emissive{sampler_,
                                             textures_[material.emissiveTextureIndex].view,
                                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        // Binding 1 is the scene buffer; all other bindings are sampled images.
        const std::array<const VkDescriptorImageInfo*, 12> images{
            &image,       nullptr,     &normal,      &metallicRoughness,
            &environment, &irradiance, &prefiltered, &brdf,
            &occlusion,   &shadow,     &emissive,    &pointShadow};
        std::array<VkWriteDescriptorSet, 12> writes{};
        for (uint32_t binding = 0; binding < writes.size(); ++binding)
        {
            auto& write = writes[binding];
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = descriptorSets_[i];
            write.dstBinding = binding;
            write.descriptorCount = 1;
            write.descriptorType = binding == 1 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                                : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pBufferInfo = binding == 1 ? &buffer : nullptr;
            write.pImageInfo = images[binding];
        }
        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0,
                               nullptr);
    }
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &postprocessDescriptorLayout_;
    check(vkAllocateDescriptorSets(device_, &allocate, &postprocessSet_),
          "Allocate postprocess set");
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
}

void VulkanRenderer::createPipeline(PipelineType type)
{
    const bool transparent = type == PipelineType::Transparent;
    const bool mesh = type == PipelineType::Mesh || transparent;
    const bool shadow = type == PipelineType::Shadow;
    const bool geometry = mesh || shadow;
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
        layout.setLayoutCount = 1;
        layout.pSetLayouts = bloom         ? &bloomDescriptorLayout_
                             : postprocess ? &postprocessDescriptorLayout_
                                           : &descriptorLayout_;
        layout.pushConstantRangeCount = postprocess ? 0 : 1;
        layout.pPushConstantRanges = &push;
        check(vkCreatePipelineLayout(device_, &layout, nullptr, &pipelineLayout),
              "Create pipeline layout");
    }

    std::array<VkShaderModule, 2> modules{};
    try
    {
        const std::string shader = shadow        ? "shadow"
                                   : mesh        ? "mesh"
                                   : bloom       ? "bloom"
                                   : postprocess ? "postprocess"
                                                 : "sky";
        modules[0] = loadShader(device_, (shader + ".vertex.spv").c_str());
        modules[1] = loadShader(device_, (shader + ".fragment.spv").c_str());
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
        const std::array<VkVertexInputAttributeDescription, 4> attributes{{
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
            {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
            {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
            {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tangent)},
        }};
        VkPipelineVertexInputStateCreateInfo vertex{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertex.vertexBindingDescriptionCount = geometry ? 1 : 0;
        vertex.pVertexBindingDescriptions = &binding;
        vertex.vertexAttributeDescriptionCount = shadow ? 2
                                                 : mesh ? static_cast<uint32_t>(attributes.size())
                                                        : 0;
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
        samples.rasterizationSamples =
            (mesh || type == PipelineType::Sky) ? sceneSamples_ : VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable = geometry;
        depth.depthWriteEnable = geometry && !transparent;
        depth.depthCompareOp = VK_COMPARE_OP_LESS;
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
        rendering.pColorAttachmentFormats = postprocess ? &format_ : &hdrFormat;
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
        vkDestroyPipeline(device_, skyPipeline_, nullptr);
        pipeline_ = transparentPipeline_ = skyPipeline_ = VK_NULL_HANDLE;
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
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((properties.optimalTilingFeatures & required) != required)
        throw std::runtime_error("RGBA16F color blending and linear sampling are unsupported");
    hdrColor_ = createImage(extent_, hdrFormat,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_IMAGE_ASPECT_COLOR_BIT);
    bloomExtent_ = {std::max((extent_.width + 1) / 2, 1u), std::max((extent_.height + 1) / 2, 1u)};
    for (auto& image : bloomImages_)
        image = createImage(bloomExtent_, hdrFormat,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_IMAGE_ASPECT_COLOR_BIT);
    const auto writeImage = [&](VkDescriptorSet set, uint32_t binding, VkImageView view)
    {
        const VkDescriptorImageInfo image{clampSampler_, view,
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = set;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    };
    writeImage(postprocessSet_, 0, hdrColor_.view);
    writeImage(postprocessSet_, 2, bloomImages_[0].view);
    for (uint32_t i = 0; i < bloomSets_.size(); ++i)
        writeImage(bloomSets_[i], 0, i == 0 ? hdrColor_.view : bloomImages_[i - 1].view);
    if (!pipeline_)
        createPipeline(PipelineType::Mesh);
    if (!transparentPipeline_)
        createPipeline(PipelineType::Transparent);
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
        if (uiContext_ && formatChanged)
        {
            ImGui_ImplVulkan_PipelineInfo uiPipeline{};
            auto& rendering = uiPipeline.PipelineRenderingCreateInfo;
            rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            rendering.colorAttachmentCount = 1;
            rendering.pColorAttachmentFormats = &format_;
            ImGui_ImplVulkan_CreateMainPipeline(&uiPipeline);
        }
    }
    resized_ = false;
}

void VulkanRenderer::recordGeometry(bool transparent, const glm::mat4& view, int pointFace)
{
    // Cube projections follow the sampler's face convention, without the screen Y flip.
    vkCmdSetFrontFace(commandBuffer_,
                      pointFace >= 0 ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer_, 0, 1, &vertexBuffer_.handle, &offset);
    vkCmdBindIndexBuffer(commandBuffer_, indexBuffer_.handle, 0, VK_INDEX_TYPE_UINT32);
    const auto draw = [&](const Draw& part, const glm::mat4& model)
    {
        const auto& material = materials_[part.materialIndex];
        if ((material.alphaMode == AlphaMode::Blend) != transparent)
            return;
        vkCmdSetCullMode(commandBuffer_,
                         material.doubleSided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT);
        const auto descriptor = descriptorSets_[part.materialIndex];
        vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0,
                                1, &descriptor, 0, nullptr);
        const PushConstants constants{
            model, material.baseColorFactor,
            glm::vec4(material.normalScale, material.metallicFactor, material.roughnessFactor,
                      settings.ambientOcclusion ? material.occlusionStrength : 0.0f),
            glm::vec4(material.emissiveFactor * material.emissiveStrength *
                          (settings.emissive ? settings.emissiveStrength : 0.0f),
                      0.0f),
            glm::vec4(material.alphaMode == AlphaMode::Mask ? material.alphaCutoff : -1.0f,
                      transparent ? 1.0f : 0.0f, float(pointFace), 0.0f)};
        vkCmdPushConstants(commandBuffer_, pipelineLayout_,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(constants), &constants);
        vkCmdDrawIndexed(commandBuffer_, part.indexCount, 1, part.firstIndex, part.vertexOffset, 0);
    };
    const auto rotation = glm::rotate(glm::mat4(1.0f), modelAngle_, glm::vec3(0, 1, 0));
    if (transparent)
    {
        std::vector<std::pair<float, const Draw*>> sorted;
        const auto modelView = view * rotation;
        for (const auto& part : draws_)
            if (materials_[part.materialIndex].alphaMode == AlphaMode::Blend)
                sorted.emplace_back((modelView * glm::vec4(part.center, 1.0f)).z, &part);
        // Visible view-space Z is negative: more negative is farther from the camera.
        std::stable_sort(sorted.begin(), sorted.end(),
                         [](const auto& a, const auto& b)
                         {
                             return a.first < b.first;
                         });
        for (const auto& [depth, part] : sorted)
            draw(*part, rotation);
    }
    else
    {
        for (const auto& part : draws_)
            draw(part, rotation);
        draw(groundDraw_, glm::mat4(1.0f));
    }
}

void VulkanRenderer::recordShadow(bool point)
{
    const auto extent = point ? pointShadowExtent : shadowExtent;
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
    barrier.image = point ? pointShadowMap_.handle : shadowMap_.handle;
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
        depth.imageView = point ? pointShadowFaces_[face] : shadowMap_.view;
        vkCmdBeginRendering(commandBuffer_, &rendering);
        if (!point || (settings.pointShadows && settings.pointLight &&
                       settings.pointIntensity > 0.0f && settings.pointRange > 0.0f))
            recordGeometry(false, glm::mat4(1.0f), point ? int(face) : -1);
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

void VulkanRenderer::record(uint32_t imageIndex, const glm::mat4& view)
{
    check(vkResetCommandBuffer(commandBuffer_, 0), "Reset command buffer");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(commandBuffer_, &begin), "Begin command buffer");

    recordShadow();
    recordShadow(true);
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
    const std::array barriers{barrier, depthBarrier, msaaBarrier};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = sceneSamples_ == VK_SAMPLE_COUNT_1_BIT ? 2 : 3;
    dependency.pImageMemoryBarriers = barriers.data();
    vkCmdPipelineBarrier2(commandBuffer_, &dependency);

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = msaaColor_.view ? msaaColor_.view : hdrColor_.view;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{0.02f, 0.04f, 0.08f, 1.0f}};
    if (sceneSamples_ != VK_SAMPLE_COUNT_1_BIT)
    {
        color.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        color.resolveImageView = hdrColor_.view;
        color.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
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
    const VkViewport viewport{
        0.0f, 0.0f, static_cast<float>(extent_.width), static_cast<float>(extent_.height),
        0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, extent_};
    vkCmdSetViewport(commandBuffer_, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer_, 0, 1, &scissor);
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline_);
    vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1,
                            descriptorSets_.data(), 0, nullptr);
    vkCmdDraw(commandBuffer_, 3, 1, 0, 0);
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    recordGeometry();
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, transparentPipeline_);
    recordGeometry(true, view);
    vkCmdEndRendering(commandBuffer_);

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
    const std::array postprocessBarriers{barrier, swapchainBarrier};
    dependency.imageMemoryBarrierCount = static_cast<uint32_t>(postprocessBarriers.size());
    dependency.pImageMemoryBarriers = postprocessBarriers.data();
    vkCmdPipelineBarrier2(commandBuffer_, &dependency);

    recordBloom();
    vkCmdSetViewport(commandBuffer_, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer_, 0, 1, &scissor);
    color.imageView = views_[imageIndex];
    color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
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
    if (auto* data = ImGui::GetDrawData())
        ImGui_ImplVulkan_RenderDrawData(data, commandBuffer_);
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

void VulkanRenderer::draw(const glm::mat4& view, const glm::vec3& cameraPosition, float deltaTime)
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
        check(vkDeviceWaitIdle(device_), "Wait before MSAA change");
        createRenderTargets();
    }
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
    auto projection = glm::perspective(
        glm::radians(45.0f), static_cast<float>(extent_.width) / static_cast<float>(extent_.height),
        0.1f, 10.0f);
    projection[1][1] *= -1.0f;
    SceneData scene{projection * view, glm::vec4(cameraPosition, 1.0f)};
    scene.lightColor = glm::vec4(glm::vec3(settings.directionalIntensity), 0.0f);
    scene.pointPositionRange = glm::vec4(settings.pointPosition, settings.pointRange);
    scene.pointColorIntensity =
        glm::vec4(settings.pointColor, settings.pointLight ? settings.pointIntensity : 0.0f);
    scene.lighting = {settings.diffuseIbl ? settings.diffuseIblStrength : 0.0f, settings.exposure,
                      settings.specularIbl ? settings.specularIblStrength : 0.0f,
                      settings.bloom ? settings.bloomStrength : 0.0f};
    scene.inverseSkyViewProjection = glm::inverse(projection * glm::mat4(glm::mat3(view)));
    auto lightProjection = glm::ortho(-4.0f, 4.0f, -4.0f, 4.0f, 0.1f, 12.0f);
    lightProjection[1][1] *= -1.0f;
    scene.lightViewProjection =
        lightProjection * glm::lookAt(glm::normalize(glm::vec3(scene.lightDirection)) * 6.0f,
                                      glm::vec3(0.0f), glm::vec3(0, 1, 0));
    scene.shadowParams = {settings.shadows ? 1.0f : 0.0f, settings.shadowBias,
                          settings.shadowPcf ? 1.0f : 0.0f, 0.0f};
    const float pointFar = std::max(settings.pointRange, 0.01f);
    const auto pointProjection =
        glm::perspective(glm::radians(90.0f), 1.0f, std::min(0.05f, pointFar * 0.01f), pointFar);
    // Vulkan cube face order: +X, -X, +Y, -Y, +Z, -Z. Do not flip projection Y.
    const std::array<glm::vec3, 6> directions{
        {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
    const std::array<glm::vec3, 6> up{
        {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}}};
    for (size_t face = 0; face < directions.size(); ++face)
        scene.pointViewProjections[face] =
            pointProjection * glm::lookAt(settings.pointPosition,
                                          settings.pointPosition + directions[face], up[face]);
    scene.pointShadowParams = {settings.pointShadows ? 1.0f : 0.0f, settings.pointShadowBias,
                               settings.pointPcf ? 1.0f : 0.0f, settings.pointFilterRadius};
    if (settings.rotateModel)
        modelAngle_ += glm::radians(45.0f) * deltaTime;
    // The frame fence has completed; no GPU submission is reading this coherent memory.
    std::memcpy(sceneMapped_, &scene, sizeof(scene));
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

void VulkanRenderer::destroyRenderTargets()
{
    destroyImage(depth_);
    destroyImage(hdrColor_);
    destroyImage(msaaColor_);
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
        if (uiContext_)
        {
            if (ImGui::GetIO().BackendRendererUserData)
                ImGui_ImplVulkan_Shutdown();
            if (ImGui::GetIO().BackendPlatformUserData)
                ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext(uiContext_);
            uiContext_ = nullptr;
        }
        destroyBuffer(indexBuffer_);
        destroyBuffer(vertexBuffer_);
        vkDestroyPipeline(device_, pipeline_, nullptr);
        vkDestroyPipeline(device_, transparentPipeline_, nullptr);
        vkDestroyPipeline(device_, shadowPipeline_, nullptr);
        vkDestroyPipeline(device_, skyPipeline_, nullptr);
        vkDestroyPipeline(device_, bloomPipeline_, nullptr);
        vkDestroyPipeline(device_, postprocessPipeline_, nullptr);
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        vkDestroyPipelineLayout(device_, bloomPipelineLayout_, nullptr);
        vkDestroyPipelineLayout(device_, postprocessPipelineLayout_, nullptr);
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        vkDestroyDescriptorSetLayout(device_, descriptorLayout_, nullptr);
        vkDestroyDescriptorSetLayout(device_, bloomDescriptorLayout_, nullptr);
        vkDestroyDescriptorSetLayout(device_, postprocessDescriptorLayout_, nullptr);
        if (sceneMapped_)
            vkUnmapMemory(device_, sceneBuffer_.memory);
        sceneMapped_ = nullptr;
        destroyBuffer(sceneBuffer_);
        vkDestroySampler(device_, sampler_, nullptr);
        vkDestroySampler(device_, shadowSampler_, nullptr);
        destroyImage(shadowMap_);
        for (auto view : pointShadowFaces_)
            vkDestroyImageView(device_, view, nullptr);
        destroyImage(pointShadowMap_);
        vkDestroySampler(device_, environmentSampler_, nullptr);
        vkDestroySampler(device_, clampSampler_, nullptr);
        destroyImage(environment_);
        destroyImage(irradiance_);
        destroyImage(prefilteredEnvironment_);
        destroyImage(brdfLut_);
        for (auto& texture : textures_)
            destroyImage(texture);
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
