#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

namespace Kosmos
{
    class Window;
    class VulkanInstance;
    class VulkanDevice;

    class VulkanImGuiRenderer
    {
        public:
            VulkanImGuiRenderer(Window& window, VulkanInstance& instance, VulkanDevice& device, VkRenderPass renderPass, uint32_t imageCount);
            ~VulkanImGuiRenderer();

            VulkanImGuiRenderer(const VulkanImGuiRenderer&) = delete;
            VulkanImGuiRenderer& operator=(const VulkanImGuiRenderer&) = delete;

            void BeginFrame();
            void EndFrame();
            void Record(VkCommandBuffer commandBuffer) const;
            void RecreatePipeline(VkRenderPass renderPass, uint32_t imageCount);

            bool WantsMouseInput() const;
            bool WantsKeyboardInput() const;

        private:
            VulkanDevice& m_Device;
    };
}