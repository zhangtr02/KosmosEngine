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
    struct RenderSettings;
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
    class VulkanDirectionalShadowPass;
    class VulkanCubeTexture;
    class VulkanSkyboxPass;
    class VulkanEnvironmentPrefilter;
    class VulkanBloomPass;
    class VulkanAutoExposurePass;
    class VulkanGBuffer;
    class VulkanDeferredLightingPass;
    class VulkanSSAOPass;

    class VulkanContext
    {
        public:
            VulkanContext(Window& window, const Camera& camera, const Scene& scene, const RenderSettings& settings);
            ~VulkanContext();

            VulkanContext(const VulkanContext&) = delete;
            VulkanContext& operator=(const VulkanContext&) = delete;

            void DrawFrame(float deltaTime);
            void WaitIdle();

        private:
            static constexpr uint32_t MaxFramesInFlight = 2;
            static constexpr VkFormat SceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

            void CreateMeshResources();
            void CreateTextureResources();
            void CreateDescriptorResources();
            void UpdateCameraUniform(uint32_t frameIndex);
            void RecreateSwapchain();
            void RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, uint32_t frameIndex, float deltaTime);
            std::unique_ptr<VulkanRenderTarget> CreateSceneRenderTarget(VkExtent2D extent);
            void RecordSceneCommands(VkCommandBuffer commandBuffer, VulkanGraphicsPipeline& pipeline, uint32_t frameIndex);
            std::unique_ptr<VulkanGraphicsPipeline> CreateGBufferPipeline(VkRenderPass renderPass, VkExtent2D extent);

        private:
            Window& m_Window;
            const Camera& m_Camera;
            const Scene& m_Scene;
            const RenderSettings& m_Settings;

            std::unique_ptr<VulkanInstance> m_Instance;
            std::unique_ptr<VulkanSurface> m_Surface;
            std::unique_ptr<VulkanDevice> m_Device;

            std::unordered_map<const Mesh*, std::unique_ptr<VulkanMesh>> m_Meshes;
            std::unordered_map<const Texture*, std::unique_ptr<VulkanTexture>> m_Textures;
            std::unique_ptr<VulkanCubeTexture> m_EnvironmentTexture;
            std::unique_ptr<VulkanEnvironmentPrefilter> m_PrefilteredEnvironment;
            std::unique_ptr<VulkanTexture> m_BrdfLutTexture;

            std::array<std::unique_ptr<VulkanBuffer>, MaxFramesInFlight> m_CameraUniformBuffers;
            std::unique_ptr<VulkanBuffer> m_LightingUniformBuffer;
            std::unique_ptr<VulkanDescriptorSetLayout> m_GlobalDescriptorSetLayout;
            std::unique_ptr<VulkanDescriptorSetLayout> m_MaterialDescriptorSetLayout;
            std::unique_ptr<VulkanDirectionalShadowPass> m_DirectionalShadowPass;
            std::unique_ptr<VulkanDescriptorPool> m_GlobalDescriptorPool;
            std::unique_ptr<VulkanDescriptorPool> m_MaterialDescriptorPool;
            std::vector<VkDescriptorSet> m_GlobalDescriptorSets;
            std::unordered_map<const Material*, std::unique_ptr<VulkanMaterial>> m_Materials;

            std::unique_ptr<VulkanSwapchain> m_Swapchain;
            std::array<std::unique_ptr<VulkanGBuffer>, MaxFramesInFlight> m_GBuffers;
            std::unique_ptr<VulkanGraphicsPipeline> m_GBufferPipeline;
            std::unique_ptr<VulkanSSAOPass> m_SSAOPass;
            std::array<std::unique_ptr<VulkanRenderTarget>, MaxFramesInFlight> m_SceneRenderTargets;
            std::unique_ptr<VulkanSkyboxPass> m_SkyboxPass;
            std::unique_ptr<VulkanDeferredLightingPass> m_DeferredLightingPass;
            std::unique_ptr<VulkanAutoExposurePass> m_AutoExposurePass;
            std::unique_ptr<VulkanBloomPass> m_BloomPass;
            std::unique_ptr<VulkanFullscreenPass> m_FullscreenPass;
            
            std::array<std::unique_ptr<VulkanFrameContext>, MaxFramesInFlight> m_FrameContexts;
            uint32_t m_CurrentFrameIndex = 0;
    };
}
