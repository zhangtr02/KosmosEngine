#include "Renderer/Vulkan/Editor/VulkanImGuiRenderer.h"
#include "Renderer/Vulkan/Core/VulkanInstance.h"
#include "Renderer/Vulkan/Core/VulkanDevice.h"
#include "Core/Window.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <stdexcept>

namespace Kosmos
{
    VulkanImGuiRenderer::VulkanImGuiRenderer(Window& window, VulkanInstance& instance, VulkanDevice& device, VkRenderPass renderPass, uint32_t imageCount)
        : m_Device(device)
    {
        if (renderPass == VK_NULL_HANDLE || imageCount < 2)
        {
            throw std::runtime_error("ImGui renderer requires valid swapchain resources!");
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        ImGui::StyleColorsDark();

        if (!ImGui_ImplGlfw_InitForVulkan(window.GetNativeWindow(), true))
        {
            ImGui::DestroyContext();
            throw std::runtime_error("Failed to initialize ImGui GLFW backend!");
        }

        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.ApiVersion = VK_API_VERSION_1_4;
        initInfo.Instance = instance.GetHandle();
        initInfo.PhysicalDevice = device.GetPhysicalDevice();
        initInfo.Device = device.GetHandle();
        initInfo.QueueFamily = device.GetQueueFamilyIndices().graphicsFamily.value();
        initInfo.Queue = device.GetGraphicsQueue();
        initInfo.DescriptorPoolSize = 256;
        initInfo.MinImageCount = imageCount;
        initInfo.ImageCount = imageCount;
        initInfo.PipelineInfoMain.RenderPass = renderPass;
        initInfo.PipelineInfoMain.Subpass = 0;
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

        if (!ImGui_ImplVulkan_Init(&initInfo))
        {
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            throw std::runtime_error("Failed to initialize ImGui Vulkan backend!");
        }
    }

    VulkanImGuiRenderer::~VulkanImGuiRenderer()
    {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    void VulkanImGuiRenderer::BeginFrame()
    {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void VulkanImGuiRenderer::EndFrame()
    {
        ImGui::Render();
    }

    void VulkanImGuiRenderer::Record(VkCommandBuffer commandBuffer) const
    {
        ImDrawData* drawData = ImGui::GetDrawData();
        if (drawData && drawData->CmdListsCount > 0)
        {
            ImGui_ImplVulkan_RenderDrawData(drawData, commandBuffer);
        }
    }

    void VulkanImGuiRenderer::RecreatePipeline(VkRenderPass renderPass, uint32_t imageCount)
    {
        ImGui_ImplVulkan_SetMinImageCount(imageCount);

        ImGui_ImplVulkan_PipelineInfo pipelineInfo{};
        pipelineInfo.RenderPass = renderPass;
        pipelineInfo.Subpass = 0;
        pipelineInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        ImGui_ImplVulkan_CreateMainPipeline(&pipelineInfo);
    }

    bool VulkanImGuiRenderer::WantsMouseInput() const
    {
        return ImGui::GetIO().WantCaptureMouse;
    }

    bool VulkanImGuiRenderer::WantsKeyboardInput() const
    {
        return ImGui::GetIO().WantCaptureKeyboard;
    }
}
