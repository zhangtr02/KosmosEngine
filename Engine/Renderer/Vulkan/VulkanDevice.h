#pragma once

#include <vulkan/vulkan.h>
#include <optional>
#include <functional>

namespace Kosmos
{
    class VulkanInstance;
    class VulkanSurface;
    class VulkanBuffer;

    struct QueueFamilyIndices
    {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;

        bool IsComplete() const
        {
            return graphicsFamily.has_value() && presentFamily.has_value();
        }
    };

    class VulkanDevice
    {
        public:
            VulkanDevice(VulkanInstance& instance, VulkanSurface& surface);
            ~VulkanDevice();

            VulkanDevice(const VulkanDevice&) = delete;
            VulkanDevice& operator=(const VulkanDevice&) = delete;

            VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
            VkDevice GetHandle() const { return m_Device; }

            VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }
            VkQueue GetPresentQueue() const { return m_PresentQueue; }

            const QueueFamilyIndices& GetQueueFamilyIndices() const { return m_QueueFamilyIndices; }

            bool TryFindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags requiredProperties, uint32_t& memoryTypeIndex) const;

            void CopyBuffer(const VulkanBuffer& source, VulkanBuffer& destination, VkDeviceSize size, VkDeviceSize sourceOffset = 0, VkDeviceSize destinationOffset = 0);
            void UploadBuffer(const void* data, VkDeviceSize size, VulkanBuffer& destination, VkDeviceSize destinationOffset = 0);
            void ExecuteSingleTimeCommands(const std::function<void(VkCommandBuffer)>& recordCommands);
            void CopyBufferToImage(
                const VulkanBuffer& source,
                VkImage destination,
                uint32_t width,
                uint32_t height,
                uint32_t arrayLayers = 1);

            void TransitionImageLayout(
                VkImage image,
                VkImageLayout oldLayout,
                VkImageLayout newLayout,
                uint32_t mipLevels = 1,
                uint32_t arrayLayers = 1);

            void GenerateMipmaps(
                VkImage image,
                VkFormat format,
                uint32_t width,
                uint32_t height,
                uint32_t mipLevels,
                uint32_t arrayLayers = 1);

            void WaitIdle() const;

        private:
            void PickPhysicalDevice();
            void CreateLogicalDevice();

            QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device) const;

            bool CheckDeviceExtensionSupport(VkPhysicalDevice device) const;
            bool HasSwapChainSupport(VkPhysicalDevice device) const;
            bool IsDeviceSuitable(VkPhysicalDevice device) const;

            VkCommandBuffer BeginSingleTimeCommands(VkCommandPool& commandPool);
            void EndSingleTimeCommands(VkCommandBuffer commandBuffer, VkCommandPool commandPool);

        private:
            VulkanInstance& m_Instance;
            VulkanSurface& m_Surface;

            VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
            QueueFamilyIndices m_QueueFamilyIndices;
            VkDevice m_Device = VK_NULL_HANDLE  ;

            VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
            VkQueue m_PresentQueue = VK_NULL_HANDLE;
    };
}