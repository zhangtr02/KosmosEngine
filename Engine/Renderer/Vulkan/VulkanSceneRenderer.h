#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Kosmos
{
    class Camera;
    class Scene;
    class Mesh;
    class Material;
    class Texture;
    struct RenderSettings;
    class VulkanDevice;
    class VulkanGraphicsPipeline;
    class VulkanBuffer;
    class VulkanDescriptorSetLayout;
    class VulkanDescriptorPool;
    class VulkanMesh;
    class VulkanMaterial;
    class VulkanTexture;
    class VulkanDirectionalShadowPass;
    class VulkanCubeTexture;
    class VulkanEnvironmentPrefilter;
    class VulkanRenderView;

    class VulkanSceneRenderer
    {
        public:
            VulkanSceneRenderer(VulkanDevice& device, const Camera& camera, const Scene& scene, const RenderSettings& settings, VkExtent2D extent, uint32_t frameCount);
            ~VulkanSceneRenderer();

            VulkanSceneRenderer(const VulkanSceneRenderer&) = delete;
            VulkanSceneRenderer& operator=(const VulkanSceneRenderer&) = delete;

            void RecreateView(VkExtent2D extent);
            void RecordFrame(VkCommandBuffer commandBuffer, uint32_t frameIndex, float deltaTime);
            std::vector<VkImageView> GetOutputImageViews() const;

        private:
            void CreateMeshResources();
            void CreateTextureResources();
            void CreateDescriptorResources();
            void UpdateCameraUniform(uint32_t frameIndex);
            void RecordSceneCommands(VkCommandBuffer commandBuffer, VulkanGraphicsPipeline& pipeline, uint32_t frameIndex);

        private:
            VulkanDevice& m_Device;
            const Camera& m_Camera;
            const Scene& m_Scene;
            const RenderSettings& m_Settings;
            uint32_t m_FrameCount = 0;

            std::unordered_map<const Mesh*, std::unique_ptr<VulkanMesh>> m_Meshes;
            std::unordered_map<const Texture*, std::unique_ptr<VulkanTexture>> m_Textures;
            std::unique_ptr<VulkanCubeTexture> m_EnvironmentTexture;
            std::unique_ptr<VulkanEnvironmentPrefilter> m_PrefilteredEnvironment;
            std::unique_ptr<VulkanTexture> m_BrdfLutTexture;

            std::vector<std::unique_ptr<VulkanBuffer>> m_CameraUniformBuffers;
            std::unique_ptr<VulkanBuffer> m_LightingUniformBuffer;
            std::unique_ptr<VulkanDescriptorSetLayout> m_GlobalDescriptorSetLayout;
            std::unique_ptr<VulkanDescriptorSetLayout> m_MaterialDescriptorSetLayout;
            std::unique_ptr<VulkanDirectionalShadowPass> m_DirectionalShadowPass;
            std::unique_ptr<VulkanDescriptorPool> m_GlobalDescriptorPool;
            std::unique_ptr<VulkanDescriptorPool> m_MaterialDescriptorPool;
            std::vector<VkDescriptorSet> m_GlobalDescriptorSets;
            std::unordered_map<const Material*, std::unique_ptr<VulkanMaterial>> m_Materials;

            std::unique_ptr<VulkanRenderView> m_RenderView;
    };
}
