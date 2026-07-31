#include "Renderer/Vulkan/Resources/VulkanMaterial.h"
#include "Renderer/Vulkan/Core/VulkanDevice.h"
#include "Renderer/Vulkan/Resources/VulkanTexture.h"
#include "Renderer/Vulkan/Resources/VulkanBuffer.h"
#include "Renderer/Vulkan/Descriptors/VulkanDescriptorPool.h"
#include "Renderer/Vulkan/Descriptors/VulkanDescriptorWriter.h"
#include "Renderer/Material.h"
#include "Renderer/MaterialUniform.h"

namespace Kosmos
{
    VulkanMaterial::VulkanMaterial(VulkanDevice& device, const Material& material, const VulkanTexture& baseColorTexture, const VulkanTexture& ormTexture, const VulkanTexture& normalTexture, VulkanDescriptorPool& descriptorPool, VkDescriptorSetLayout descriptorSetLayout)
    {
        m_UniformBuffer = std::make_unique<VulkanBuffer>(device, sizeof(MaterialUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        MaterialUniform materialUniform{};
        materialUniform.baseColor = material.GetBaseColor();
        materialUniform.metallic = material.GetMetallic();
        materialUniform.roughness = material.GetRoughness();
        materialUniform.ambientOcclusion = material.GetAmbientOcclusion();
        materialUniform.emissiveStrength = material.GetEmissiveStrength();
        m_UniformBuffer->Write(&materialUniform, sizeof(materialUniform));

        m_DescriptorSet = descriptorPool.AllocateSets(descriptorSetLayout, 1).front();

        VulkanDescriptorWriter writer(device);
        writer.WriteBuffer(m_DescriptorSet, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, m_UniformBuffer->GetHandle(), 0, sizeof(MaterialUniform));
        writer.WriteImage(m_DescriptorSet, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, baseColorTexture.GetImageView(), baseColorTexture.GetSampler(), baseColorTexture.GetLayout());
        writer.WriteImage(m_DescriptorSet, 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, ormTexture.GetImageView(), ormTexture.GetSampler(), ormTexture.GetLayout());
        writer.WriteImage(m_DescriptorSet, 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, normalTexture.GetImageView(), normalTexture.GetSampler(), normalTexture.GetLayout());
    }

    VulkanMaterial::~VulkanMaterial() = default;
}