#include "Renderer/Vulkan/VulkanDescriptorSetLayout.h"
#include "Renderer/Vulkan/VulkanDevice.h"

#include <stdexcept>

namespace Kosmos
{
    VulkanDescriptorSetLayout::VulkanDescriptorSetLayout(VulkanDevice& device, const std::vector<VkDescriptorSetLayoutBinding>& bindings)
        : m_Device(device)
    {
        if (bindings.empty())
        {
            throw std::runtime_error("Cannot create an empty descriptor set layout!");
        }

        VkDescriptorSetLayoutCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        createInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        createInfo.pBindings = bindings.data();

        if (vkCreateDescriptorSetLayout(m_Device.GetHandle(), &createInfo, nullptr, &m_Layout) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create descriptor set layout!");
        }
    }

    VulkanDescriptorSetLayout::~VulkanDescriptorSetLayout()
    {
        if (m_Layout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(m_Device.GetHandle(), m_Layout, nullptr);
        }
    }
}