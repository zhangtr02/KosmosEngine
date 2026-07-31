#pragma once

#include <vulkan/vulkan.h>
#include <memory>

namespace Kosmos
{
    class Material;
    class VulkanDevice;
    class VulkanTexture;
    class VulkanBuffer;
    class VulkanDescriptorPool;

    class VulkanMaterial
    {
        public:
            VulkanMaterial(VulkanDevice& device, const Material& material, const VulkanTexture& baseColorTexture, const VulkanTexture& ormTexture, const VulkanTexture& normalTexture, VulkanDescriptorPool& descriptorPool, VkDescriptorSetLayout descriptorSetLayout);
            ~VulkanMaterial();

            VulkanMaterial(const VulkanMaterial&) = delete;
            VulkanMaterial& operator=(const VulkanMaterial&) = delete;

            VkDescriptorSet GetDescriptorSet() const { return m_DescriptorSet; }

        private:
            std::unique_ptr<VulkanBuffer> m_UniformBuffer;
            VkDescriptorSet m_DescriptorSet = VK_NULL_HANDLE;
    };
}