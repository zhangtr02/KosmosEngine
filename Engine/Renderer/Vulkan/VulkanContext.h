#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <array>
#include <vector>
#include <unordered_map>

namespace Kosmos
{
    class Window;
    class VulkanInstance;
    class VulkanSurface;
    class VulkanDevice;
    class VulkanSwapchain;
    class VulkanPipeline;
    class VulkanFrameContext;
    class VulkanBuffer;
    class VulkanDescriptorSetLayout;
    class VulkanDescriptorPool;
    class Camera;
    class Scene;
    class Mesh;
    class VulkanMesh;
    class Texture;
    class VulkanTexture;

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

            void CreateMeshResources();
            void CreateTextureResources();
            void CreateDescriptorResources();
            void UpdateCameraUniform(uint32_t frameIndex);
            void RecreateSwapchain();
            void RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, uint32_t frameIndex);

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
            std::unique_ptr<VulkanDescriptorSetLayout> m_DescriptorSetLayout;
            std::unique_ptr<VulkanDescriptorPool> m_DescriptorPool;
            std::vector<VkDescriptorSet> m_DescriptorSets;

            std::unique_ptr<VulkanSwapchain> m_Swapchain;
            std::unique_ptr<VulkanPipeline> m_Pipeline;
            
            std::array<std::unique_ptr<VulkanFrameContext>, MaxFramesInFlight> m_FrameContexts;
            uint32_t m_CurrentFrameIndex = 0;
    };
}