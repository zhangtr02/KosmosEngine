#include "Renderer/Vulkan/VulkanMaterial.h"
#include "Renderer/Vulkan/VulkanDevice.h"
#include "Renderer/Vulkan/VulkanTexture.h"
#include "Renderer/Vulkan/VulkanBuffer.h"
#include "Renderer/Vulkan/VulkanDescriptorPool.h"
#include "Renderer/Vulkan/VulkanDescriptorWriter.h"
#include "Renderer/Material.h"
#include "Renderer/MaterialUniform.h"

namespace Kosmos
{
    VulkanMaterial::VulkanMaterial(VulkanDevice& device, const Material& material, const VulkanTexture& texture, VulkanDescriptorPool& descriptorPool, VkDescriptorSetLayout descriptorSetLayout)
    {
        m_UniformBuffer = std::make_unique<VulkanBuffer>(device, sizeof(MaterialUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        MaterialUniform materialUniform{};
        materialUniform.baseColor = material.GetBaseColor();
        m_UniformBuffer->Write(&materialUniform, sizeof(materialUniform));

        m_DescriptorSet = descriptorPool.AllocateSets(descriptorSetLayout, 1).front();

        VulkanDescriptorWriter writer(device);
        writer.WriteBuffer(m_DescriptorSet, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, m_UniformBuffer->GetHandle(), 0, sizeof(MaterialUniform));
        writer.WriteImage(m_DescriptorSet, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, texture.GetImageView(), texture.GetSampler(), texture.GetLayout());
    }

    VulkanMaterial::~VulkanMaterial() = default;
}