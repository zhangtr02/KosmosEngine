#pragma once

#include <vulkan/vulkan.h>

namespace Kosmos
{
    class VulkanDevice;

    class VulkanDescriptorWriter
    {
        public:
            explicit VulkanDescriptorWriter(VulkanDevice& device);

            VulkanDescriptorWriter(const VulkanDescriptorWriter&) = delete;
            VulkanDescriptorWriter& operator=(const VulkanDescriptorWriter&) = delete;

            void WriteBuffer(VkDescriptorSet descriptorSet, uint32_t binding, VkDescriptorType descriptorType, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range, uint32_t arrayElement = 0);
            void WriteImage(VkDescriptorSet descriptorSet, uint32_t binding, VkDescriptorType descriptorType, VkImageView imageView, VkSampler sampler, VkImageLayout imageLayout, uint32_t arrayElement = 0);

        private:
            VulkanDevice& m_Device;
    };
}