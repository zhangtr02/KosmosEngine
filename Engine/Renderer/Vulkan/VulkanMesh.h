#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <memory>

namespace Kosmos
{
    class Mesh;
    class VulkanDevice;
    class VulkanBuffer;

    class VulkanMesh
    {
        public:
            VulkanMesh(VulkanDevice& device, const Mesh& mesh);
            ~VulkanMesh() = default;

            VulkanMesh(const VulkanMesh&) = delete;
            VulkanMesh& operator=(const VulkanMesh&) = delete;

            void Bind(VkCommandBuffer commandBuffer) const;
            void Draw(VkCommandBuffer commandBuffer) const;

        private:
            std::unique_ptr<VulkanBuffer> m_VertexBuffer;
            std::unique_ptr<VulkanBuffer> m_IndexBuffer;
            uint32_t m_IndexCount = 0;
    };
}