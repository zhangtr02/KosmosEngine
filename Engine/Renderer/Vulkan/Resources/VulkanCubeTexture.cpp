#include "Renderer/Vulkan/Resources/VulkanCubeTexture.h"
#include "Renderer/Vulkan/Resources/VulkanImage.h"
#include "Renderer/Vulkan/Resources/VulkanBuffer.h"
#include "Renderer/Vulkan/Core/VulkanDevice.h"
#include "Renderer/CubeTexture.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>
#include <cstring>

namespace Kosmos
{
    VulkanCubeTexture::VulkanCubeTexture(VulkanDevice& device, const CubeTexture& texture)
        : m_Device(device)
    {
        m_MipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texture.GetWidth(), texture.GetHeight())))) + 1;

        const size_t faceByteSize = texture.GetFaces()[0]->GetByteSize();
        std::vector<uint8_t> uploadData(faceByteSize * CubeTexture::FaceCount);

        for (uint32_t faceIndex = 0; faceIndex < CubeTexture::FaceCount; ++faceIndex) std::memcpy(uploadData.data() + faceByteSize * faceIndex, texture.GetFaces()[faceIndex]->GetData(), faceByteSize);

        VulkanBuffer stagingBuffer(m_Device, static_cast<VkDeviceSize>(uploadData.size()), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        stagingBuffer.Write(uploadData.data(), static_cast<VkDeviceSize>(uploadData.size()));

        const VkFormat format = texture.GetFaces()[0]->GetDataType() == TextureDataType::Float32 ? VK_FORMAT_R32G32B32A32_SFLOAT : texture.GetColorSpace() == TextureColorSpace::SRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

        m_Image = std::make_unique<VulkanImage>(m_Device, texture.GetWidth(), texture.GetHeight(), format, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, m_MipLevels, CubeTexture::FaceCount, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, VK_IMAGE_VIEW_TYPE_CUBE);
        m_Device.TransitionImageLayout(m_Image->GetHandle(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, m_MipLevels, CubeTexture::FaceCount);
        m_Device.CopyBufferToImage(stagingBuffer, m_Image->GetHandle(), texture.GetWidth(), texture.GetHeight(), CubeTexture::FaceCount);
        m_Device.GenerateMipmaps(m_Image->GetHandle(), format, texture.GetWidth(), texture.GetHeight(), m_MipLevels, CubeTexture::FaceCount);

        VkPhysicalDeviceProperties deviceProperties{};
        vkGetPhysicalDeviceProperties(m_Device.GetPhysicalDevice(), &deviceProperties);

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = deviceProperties.limits.maxSamplerAnisotropy;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = static_cast<float>(m_MipLevels - 1);
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        if (vkCreateSampler(m_Device.GetHandle(), &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS) throw std::runtime_error("Failed to create Vulkan cube texture sampler!");
    }

    VulkanCubeTexture::~VulkanCubeTexture()
    {
        if (m_Sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(m_Device.GetHandle(), m_Sampler, nullptr);
        }
    }

    VkImageView VulkanCubeTexture::GetImageView() const
    {
        return m_Image->GetImageView();
    }
}