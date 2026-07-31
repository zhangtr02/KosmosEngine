#pragma once

#include <vulkan/vulkan.h>
#include <vector>

namespace Kosmos
{
    class VulkanDevice;

    class VulkanDescriptorSetLayout
    {
        public:
            VulkanDescriptorSetLayout(VulkanDevice& device, const std::vector<VkDescriptorSetLayoutBinding>& bindings);
            ~VulkanDescriptorSetLayout();

            VulkanDescriptorSetLayout(const VulkanDescriptorSetLayout&) = delete;
            VulkanDescriptorSetLayout& operator=(const VulkanDescriptorSetLayout&) = delete;

            VkDescriptorSetLayout GetHandle() const { return m_Layout; }

        private:
            VulkanDevice& m_Device;
            VkDescriptorSetLayout m_Layout = VK_NULL_HANDLE;
    };
}