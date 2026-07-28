#pragma once

#include <vulkan/vulkan.h>
#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Kosmos
{
    class Window;
    class Camera;
    class Scene;
    class Mesh;
    class Material;
    class Texture;
    class VulkanInstance;
    class VulkanSurface;
    class VulkanDevice;
    class VulkanSwapchain;
    class VulkanGraphicsPipeline;
    class VulkanFrameContext;
    class VulkanBuffer;
    class VulkanDescriptorSetLayout;
    class VulkanDescriptorPool;
    class VulkanMesh;
    class VulkanMaterial;
    class VulkanTexture;
    class VulkanRenderTarget;
    class VulkanFullscreenPass;

    class VulkanContext
    {
        public:
            explicit VulkanContext(Window& window, const Camera& camera, const Scene& scene);
            ~VulkanContext();

            VulkanContext(const VulkanContext&) = delete;
            VulkanContext& operator=(const VulkanContext&) = delete;

            void DrawFrame();
            void WaitIdle();

        private:
            static constexpr uint32_t MaxFramesInFlight = 2;
            static constexpr VkFormat SceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

            void CreateMeshResources();
            void CreateTextureResources();
            void CreateDescriptorResources();
            void UpdateCameraUniform(uint32_t frameIndex);
            void RecreateSwapchain();
            void RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, uint32_t frameIndex);
            std::unique_ptr<VulkanRenderTarget> CreateSceneRenderTarget(VkExtent2D extent);
            std::unique_ptr<VulkanGraphicsPipeline> CreateForwardPipeline(VkRenderPass renderPass, VkExtent2D extent);
            void RecordSceneCommands(VkCommandBuffer commandBuffer, VulkanGraphicsPipeline& pipeline, uint32_t frameIndex);

        private:
            Window& m_Window;
            const Camera& m_Camera;
            const Scene& m_Scene;

            std::unique_ptr<VulkanInstance> m_Instance;
            std::unique_ptr<VulkanSurface> m_Surface;
            std::unique_ptr<VulkanDevice> m_Device;

            std::unordered_map<const Mesh*, std::unique_ptr<VulkanMesh>> m_Meshes;
            std::unordered_map<const Texture*, std::unique_ptr<VulkanTexture>> m_Textures;

            std::array<std::unique_ptr<VulkanBuffer>, MaxFramesInFlight> m_CameraUniformBuffers;
            std::unique_ptr<VulkanBuffer> m_LightingUniformBuffer;
            std::unique_ptr<VulkanDescriptorSetLayout> m_GlobalDescriptorSetLayout;
            std::unique_ptr<VulkanDescriptorSetLayout> m_MaterialDescriptorSetLayout;
            std::unique_ptr<VulkanDescriptorPool> m_GlobalDescriptorPool;
            std::unique_ptr<VulkanDescriptorPool> m_MaterialDescriptorPool;
            std::vector<VkDescriptorSet> m_GlobalDescriptorSets;
            std::unordered_map<const Material*, std::unique_ptr<VulkanMaterial>> m_Materials;

            std::unique_ptr<VulkanSwapchain> m_Swapchain;
            std::array<std::unique_ptr<VulkanRenderTarget>, MaxFramesInFlight> m_SceneRenderTargets;
            std::unique_ptr<VulkanGraphicsPipeline> m_ScenePipeline;
            std::unique_ptr<VulkanFullscreenPass> m_FullscreenPass;
            
            std::array<std::unique_ptr<VulkanFrameContext>, MaxFramesInFlight> m_FrameContexts;
            uint32_t m_CurrentFrameIndex = 0;
    };
}