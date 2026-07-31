#include "Renderer/Vulkan/Resources/VulkanMesh.h"
#include "Renderer/Vulkan/Resources/VulkanBuffer.h"
#include "Renderer/Vulkan/Core/VulkanDevice.h"
#include "Renderer/Resources/Mesh.h"
#include "Renderer/Resources/Vertex.h"

namespace Kosmos
{
    VulkanMesh::VulkanMesh(VulkanDevice& device, const Mesh& mesh)
    {
        const std::vector<Vertex>& vertices = mesh.GetVertices();
        const std::vector<uint32_t>& indices = mesh.GetIndices();

        const VkDeviceSize vertexBufferSize = sizeof(Vertex) * vertices.size();

        m_VertexBuffer = std::make_unique<VulkanBuffer>(
            device,
            vertexBufferSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        device.UploadBuffer(vertices.data(), vertexBufferSize, *m_VertexBuffer);

        const VkDeviceSize indexBufferSize = sizeof(uint32_t) * indices.size();

        m_IndexBuffer = std::make_unique<VulkanBuffer>(
            device,
            indexBufferSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        device.UploadBuffer(indices.data(), indexBufferSize, *m_IndexBuffer);

        m_IndexCount = static_cast<uint32_t>(indices.size());
    }

    void VulkanMesh::Bind(VkCommandBuffer commandBuffer) const
    {
        const VkBuffer vertexBuffers[] = {
            m_VertexBuffer->GetHandle()
        };

        const VkDeviceSize offsets[] = {
            0
        };

        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(commandBuffer, m_IndexBuffer->GetHandle(), 0, VK_INDEX_TYPE_UINT32);
    }

    void VulkanMesh::Draw(VkCommandBuffer commandBuffer) const
    {
        vkCmdDrawIndexed(commandBuffer, m_IndexCount, 1, 0, 0, 0);
    }
}