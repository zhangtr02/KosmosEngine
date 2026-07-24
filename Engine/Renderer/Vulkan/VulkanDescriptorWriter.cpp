#include "Renderer/Vulkan/VulkanDescriptorWriter.h"
#include "Renderer/Vulkan/VulkanDevice.h"

#include <stdexcept>

namespace Kosmos
{
    VulkanDescriptorWriter::VulkanDescriptorWriter(VulkanDevice& device)
        : m_Device(device)
    {
    }

    void VulkanDescriptorWriter::WriteBuffer(VkDescriptorSet descriptorSet, uint32_t binding, VkDescriptorType descriptorType, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range, uint32_t arrayElement)
    {
        if (descriptorSet == VK_NULL_HANDLE)
        {
            throw std::runtime_error("Cannot write to a null descriptor set!");
        }

        if (buffer == VK_NULL_HANDLE)
        {
            throw std::runtime_error("Cannot write a null buffer descriptor!");
        }

        if (range == 0)
        {
            throw std::runtime_error("Descriptor buffer range must be greater than zero!");
        }

        const bool isBufferDescriptor =
            descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
            descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
            descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
            descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;

        if (!isBufferDescriptor)
        {
            throw std::runtime_error("Descriptor type is not a supported buffer descriptor!");
        }

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = buffer;
        bufferInfo.offset = offset;
        bufferInfo.range = range;

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descriptorSet;
        descriptorWrite.dstBinding = binding;
        descriptorWrite.dstArrayElement = arrayElement;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.descriptorType = descriptorType;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(m_Device.GetHandle(), 1, &descriptorWrite, 0, nullptr);
    }

    void VulkanDescriptorWriter::WriteImage(VkDescriptorSet descriptorSet, uint32_t binding, VkDescriptorType descriptorType, VkImageView imageView, VkSampler sampler, VkImageLayout imageLayout, uint32_t arrayElement)
    {
        if (descriptorSet == VK_NULL_HANDLE)
        {
            throw std::runtime_error("Cannot write to a null descriptor set!");
        }

        if (imageView == VK_NULL_HANDLE)
        {
            throw std::runtime_error("Cannot write a null image view descriptor!");
        }

        const bool isImageDescriptor =
            descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
            descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
            descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ||
            descriptorType == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;

        if (!isImageDescriptor)
        {
            throw std::runtime_error("Descriptor type is not a supported image descriptor!");
        }

        if (descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER && sampler == VK_NULL_HANDLE)
        {
            throw std::runtime_error("Combined image sampler descriptor requires a sampler!");
        }

        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler = sampler;
        imageInfo.imageView = imageView;
        imageInfo.imageLayout = imageLayout;

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descriptorSet;
        descriptorWrite.dstBinding = binding;
        descriptorWrite.dstArrayElement = arrayElement;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.descriptorType = descriptorType;
        descriptorWrite.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(m_Device.GetHandle(), 1, &descriptorWrite, 0, nullptr);
    }
}