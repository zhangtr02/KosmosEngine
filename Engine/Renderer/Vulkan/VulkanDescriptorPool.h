#pragma once

#include <vulkan/vulkan.h>
#include <vector>

namespace Kosmos
{
    class VulkanDevice;

    class VulkanDescriptorPool
    {
        public:
            VulkanDescriptorPool(VulkanDevice& device, uint32_t maxSets, const std::vector<VkDescriptorPoolSize>& poolSizes);
            ~VulkanDescriptorPool();

            VulkanDescriptorPool(const VulkanDescriptorPool&) = delete;
            VulkanDescriptorPool& operator=(const VulkanDescriptorPool&) = delete;

            std::vector<VkDescriptorSet> AllocateSets(VkDescriptorSetLayout layout, uint32_t count);

            VkDescriptorPool GetHandle() const { return m_Pool; }

        private:
            VulkanDevice& m_Device;
            VkDescriptorPool m_Pool = VK_NULL_HANDLE;
    };
}