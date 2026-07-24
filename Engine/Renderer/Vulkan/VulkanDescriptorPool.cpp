#include "Renderer/Vulkan/VulkanDescriptorPool.h"
#include "Renderer/Vulkan/VulkanDevice.h"

#include <stdexcept>

namespace Kosmos
{
    VulkanDescriptorPool::VulkanDescriptorPool(VulkanDevice& device, uint32_t maxSets, const std::vector<VkDescriptorPoolSize>& poolSizes)
        : m_Device(device)
    {
        if (maxSets == 0)
        {
            throw std::runtime_error("Descriptor pool max set count must be greater than zero!");
        }

        if (poolSizes.empty())
        {
            throw std::runtime_error("Descriptor pool must contain at least one descriptor type!");
        }

        for (const VkDescriptorPoolSize& poolSize : poolSizes)
        {
            if (poolSize.descriptorCount == 0)
            {
                throw std::runtime_error("Descriptor pool size must be greater than zero!");
            }
        }

        VkDescriptorPoolCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        createInfo.maxSets = maxSets;
        createInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        createInfo.pPoolSizes = poolSizes.data();

        if (vkCreateDescriptorPool(m_Device.GetHandle(), &createInfo, nullptr, &m_Pool) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create descriptor pool!");
        }
    }

    VulkanDescriptorPool::~VulkanDescriptorPool()
    {
        if (m_Pool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(m_Device.GetHandle(), m_Pool, nullptr);
        }
    }

    std::vector<VkDescriptorSet> VulkanDescriptorPool::AllocateSets(VkDescriptorSetLayout layout, uint32_t count)
    {
        if (layout == VK_NULL_HANDLE)
        {
            throw std::runtime_error("Cannot allocate descriptor sets with a null layout!");
        }

        if (count == 0)
        {
            throw std::runtime_error("Descriptor set count must be greater than zero!");
        }

        std::vector<VkDescriptorSetLayout> layouts(count, layout);
        std::vector<VkDescriptorSet> descriptorSets(count, VK_NULL_HANDLE);

        VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.descriptorPool = m_Pool;
        allocateInfo.descriptorSetCount = count;
        allocateInfo.pSetLayouts = layouts.data();

        if (vkAllocateDescriptorSets(m_Device.GetHandle(), &allocateInfo, descriptorSets.data()) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate descriptor sets!");
        }

        return descriptorSets;
    }
}