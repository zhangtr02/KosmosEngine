#include "Renderer/Vulkan/VulkanContext.h"
#include "Core/Window.h"
#include "Renderer/Vulkan/VulkanInstance.h"
#include "Renderer/Vulkan/VulkanSurface.h"
#include "Renderer/Vulkan/VulkanDevice.h"
#include "Renderer/Vulkan/VulkanSwapchain.h"
#include "Renderer/Vulkan/VulkanPipeline.h"
#include "Renderer/Vulkan/VulkanFrameContext.h"
#include "Renderer/CameraUniform.h"
#include "Renderer/Vulkan/VulkanDescriptorSetLayout.h"
#include "Renderer/Vulkan/VulkanDescriptorPool.h"
#include "Renderer/Vulkan/VulkanDescriptorWriter.h"
#include "Renderer/Vulkan/VulkanBuffer.h"
#include "Renderer/ObjectPushConstant.h"
#include "Renderer/Mesh.h"
#include "Renderer/Vulkan/VulkanMesh.h"
#include "Renderer/Texture.h"
#include "Renderer/Vulkan/VulkanTexture.h"
#include "Renderer/Material.h"
#include "Renderer/MaterialUniform.h"
#include "Renderer/Vulkan/VulkanMaterial.h"
#include "Renderer/LightingUniform.h"
#include "Scene/Light.h"
#include "Scene/Scene.h"
#include "Scene/Camera.h"

#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <vector>
#include <memory>
#include <stdexcept>
#include <utility>
#include <array>
#include <unordered_set>

namespace Kosmos
{
    VulkanContext::VulkanContext(Window& window, const Camera& camera, const Scene& scene)
        : m_Window(window), m_Camera(camera), m_Scene(scene)
    {
        m_Instance = std::make_unique<VulkanInstance>();
        m_Surface = std::make_unique<VulkanSurface>(*m_Instance, m_Window);
        m_Device = std::make_unique<VulkanDevice>(*m_Instance, *m_Surface);

        CreateMeshResources();
        CreateTextureResources();
        CreateDescriptorResources();

        m_Swapchain = std::make_unique<VulkanSwapchain>(m_Window, *m_Device, *m_Surface);
        m_Pipeline = std::make_unique<VulkanPipeline>(*m_Device, m_Swapchain->GetRenderPass(), m_Swapchain->GetExtent(), m_GlobalDescriptorSetLayout->GetHandle(), m_MaterialDescriptorSetLayout->GetHandle());

        for (std::unique_ptr<VulkanFrameContext>& frameContext : m_FrameContexts)
        {
            frameContext = std::make_unique<VulkanFrameContext>(*m_Device);
        }
    }

    VulkanContext::~VulkanContext()
    {
        if (m_Device && m_Device->GetHandle() != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(m_Device->GetHandle());
        }
    }

    void VulkanContext::CreateMeshResources()
    {
        for (const RenderObject& object : m_Scene.GetRenderObjects())
        {
            if (!object.mesh)
            {
                throw std::runtime_error("Scene contains a render object without a mesh!");
            }

            const Mesh* mesh = object.mesh.get();

            if (!m_Meshes.contains(mesh))
            {
                m_Meshes.emplace(mesh, std::make_unique<VulkanMesh>(*m_Device, *mesh));
            }
        }
    }

    void VulkanContext::CreateTextureResources()
    {
        for (const RenderObject& object : m_Scene.GetRenderObjects())
        {
            if (!object.material)
            {
                throw std::runtime_error("Scene contains a render object without a material!");
            }

            const std::shared_ptr<Texture>& texture = object.material->GetBaseColorTexture();

            if (!texture)
            {
                throw std::runtime_error("Material contains a null base color texture!");
            }

            const Texture* textureHandle = texture.get();

            if (!m_Textures.contains(textureHandle))
            {
                m_Textures.emplace(textureHandle, std::make_unique<VulkanTexture>(*m_Device, *textureHandle));
            }
        }
    }

    void VulkanContext::CreateDescriptorResources()
    {
        std::unordered_set<const Material*> materialHandles;

        for (const RenderObject& object : m_Scene.GetRenderObjects())
        {
            if (!object.material)
            {
                throw std::runtime_error("Scene contains a render object without a material!");
            }

            materialHandles.insert(object.material.get());
        }

        if (materialHandles.empty())
        {
            throw std::runtime_error("Basic material rendering requires at least one material!");
        }

        VkDescriptorSetLayoutBinding cameraBinding{};
        cameraBinding.binding = 0;
        cameraBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        cameraBinding.descriptorCount = 1;
        cameraBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutBinding lightingBinding{};
        lightingBinding.binding = 1;
        lightingBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        lightingBinding.descriptorCount = 1;
        lightingBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        m_GlobalDescriptorSetLayout = std::make_unique<VulkanDescriptorSetLayout>(*m_Device, std::vector<VkDescriptorSetLayoutBinding>{cameraBinding, lightingBinding});

        VkDescriptorSetLayoutBinding materialBinding{};
        materialBinding.binding = 0;
        materialBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        materialBinding.descriptorCount = 1;
        materialBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding textureBinding{};
        textureBinding.binding = 1;
        textureBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        textureBinding.descriptorCount = 1;
        textureBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        m_MaterialDescriptorSetLayout = std::make_unique<VulkanDescriptorSetLayout>(*m_Device, std::vector<VkDescriptorSetLayoutBinding>{materialBinding, textureBinding});

        for (std::unique_ptr<VulkanBuffer>& uniformBuffer : m_CameraUniformBuffers)
        {
            uniformBuffer = std::make_unique<VulkanBuffer>(*m_Device, sizeof(CameraUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        }

        const SceneLighting& sceneLighting = m_Scene.GetLighting();

        LightingUniform lightingUniform{};
        lightingUniform.ambient = glm::vec4(sceneLighting.ambientColor, sceneLighting.ambientIntensity);
        lightingUniform.directionalDirection = glm::vec4(glm::normalize(sceneLighting.directionalLight.direction), 0.0f);
        lightingUniform.directionalColor = glm::vec4(sceneLighting.directionalLight.color, sceneLighting.directionalLight.intensity);
        lightingUniform.pointPosition = glm::vec4(sceneLighting.pointLight.position, 1.0f);
        lightingUniform.pointColor = glm::vec4(sceneLighting.pointLight.color, sceneLighting.pointLight.intensity);
        lightingUniform.pointAttenuation = glm::vec4(sceneLighting.pointLight.constantAttenuation, sceneLighting.pointLight.linearAttenuation, sceneLighting.pointLight.quadraticAttenuation, 0.0f);

        m_LightingUniformBuffer = std::make_unique<VulkanBuffer>(*m_Device, sizeof(LightingUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        m_LightingUniformBuffer->Write(&lightingUniform, sizeof(lightingUniform));

        VkDescriptorPoolSize globalUniformPoolSize{};
        globalUniformPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        globalUniformPoolSize.descriptorCount = MaxFramesInFlight * 2;

        m_GlobalDescriptorPool = std::make_unique<VulkanDescriptorPool>(*m_Device, MaxFramesInFlight, std::vector<VkDescriptorPoolSize>{globalUniformPoolSize});
        m_GlobalDescriptorSets = m_GlobalDescriptorPool->AllocateSets(m_GlobalDescriptorSetLayout->GetHandle(), MaxFramesInFlight);

        VulkanDescriptorWriter writer(*m_Device);

        for (uint32_t frameIndex = 0; frameIndex < MaxFramesInFlight; ++frameIndex)
        {
            writer.WriteBuffer(m_GlobalDescriptorSets[frameIndex], 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, m_CameraUniformBuffers[frameIndex]->GetHandle(), 0, sizeof(CameraUniform));
            writer.WriteBuffer(m_GlobalDescriptorSets[frameIndex], 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, m_LightingUniformBuffer->GetHandle(), 0, sizeof(LightingUniform));
        }

        const uint32_t materialCount = static_cast<uint32_t>(materialHandles.size());

        VkDescriptorPoolSize materialUniformPoolSize{};
        materialUniformPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        materialUniformPoolSize.descriptorCount = materialCount;

        VkDescriptorPoolSize texturePoolSize{};
        texturePoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        texturePoolSize.descriptorCount = materialCount;

        m_MaterialDescriptorPool = std::make_unique<VulkanDescriptorPool>(*m_Device, materialCount, std::vector<VkDescriptorPoolSize>{materialUniformPoolSize, texturePoolSize});

        for (const Material* material : materialHandles)
        {
            const std::shared_ptr<Texture>& texture = material->GetBaseColorTexture();
            const auto textureIterator = m_Textures.find(texture.get());

            if (textureIterator == m_Textures.end())
            {
                throw std::runtime_error("Material texture does not have a Vulkan texture resource!");
            }

            m_Materials.emplace(material, std::make_unique<VulkanMaterial>(*m_Device, *material, *textureIterator->second, *m_MaterialDescriptorPool, m_MaterialDescriptorSetLayout->GetHandle()));
        }
    }

    void VulkanContext::UpdateCameraUniform(uint32_t frameIndex)
    {
        const VkExtent2D extent = m_Swapchain->GetExtent();
        const float aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);

        CameraUniform cameraUniform{};
        cameraUniform.view = m_Camera.GetViewMatrix();
        cameraUniform.projection = m_Camera.GetProjectionMatrix(aspectRatio);
        cameraUniform.projection[1][1] *= -1.0f;

        m_CameraUniformBuffers[frameIndex]->Write(&cameraUniform, sizeof(CameraUniform));
    }

    void VulkanContext::RecreateSwapchain()
    {
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        m_Window.GetFramebufferSize(framebufferWidth, framebufferHeight);

        while (framebufferWidth == 0 || framebufferHeight == 0)
        {
            if (m_Window.ShouldClose())
            {
                return;
            }

            m_Window.WaitEvents();
            m_Window.GetFramebufferSize(framebufferWidth, framebufferHeight);
        }

        if (m_Window.ShouldClose())
        {
            return;
        }

        const VkSwapchainKHR oldSwapchain = m_Swapchain->GetHandle();
        auto newSwapchain = std::make_unique<VulkanSwapchain>(m_Window, *m_Device, *m_Surface, oldSwapchain);
        auto newPipeline = std::make_unique<VulkanPipeline>(*m_Device, newSwapchain->GetRenderPass(), newSwapchain->GetExtent(), m_GlobalDescriptorSetLayout->GetHandle(), m_MaterialDescriptorSetLayout->GetHandle());

        m_Device->WaitIdle();

        m_Pipeline = std::move(newPipeline);
        m_Swapchain = std::move(newSwapchain);
    }

    void VulkanContext::RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex, uint32_t frameIndex)
    {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to begin command buffer!");
        }

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{0.02f, 0.03f, 0.04f, 1.0f}};
        clearValues[1].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = m_Swapchain->GetRenderPass();
        renderPassInfo.framebuffer = m_Swapchain->GetFramebuffer(imageIndex);
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = m_Swapchain->GetExtent();
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline->GetHandle());

        const VkDescriptorSet globalDescriptorSet = m_GlobalDescriptorSets[frameIndex];
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline->GetLayout(), 0, 1, &globalDescriptorSet, 0, nullptr);

        const VulkanMesh* boundMesh = nullptr;
        const VulkanMaterial* boundMaterial = nullptr;

        for (const RenderObject& object : m_Scene.GetRenderObjects())
        {
            const VulkanMesh& mesh = *m_Meshes.at(object.mesh.get());
            const VulkanMaterial& material = *m_Materials.at(object.material.get());

            if (boundMesh != &mesh)
            {
                mesh.Bind(commandBuffer);
                boundMesh = &mesh;
            }

            if (boundMaterial != &material)
            {
                const VkDescriptorSet materialDescriptorSet = material.GetDescriptorSet();
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline->GetLayout(), 1, 1, &materialDescriptorSet, 0, nullptr);
                boundMaterial = &material;
            }

            ObjectPushConstant objectPushConstant{};
            objectPushConstant.model = object.transform.GetMatrix();
            objectPushConstant.normalMatrix = glm::transpose(glm::inverse(objectPushConstant.model));

            vkCmdPushConstants(commandBuffer, m_Pipeline->GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ObjectPushConstant), &objectPushConstant);
            mesh.Draw(commandBuffer);
        }

        vkCmdEndRenderPass(commandBuffer);

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to record command buffer!");
        }
    }

    void VulkanContext::DrawFrame()
    {
        VulkanFrameContext& frame = *m_FrameContexts[m_CurrentFrameIndex];
        frame.WaitForFence();

        uint32_t imageIndex = 0;

        const VkResult acquireResult = m_Swapchain->AcquireNextImage(frame.GetImageAvailableSemaphore(), imageIndex);

        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
        {
            m_Window.ResetFramebufferResized();
            RecreateSwapchain();
            return;
        }

        if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
        {
            throw std::runtime_error("Failed to acquire swapchain image!");
        }

        UpdateCameraUniform(m_CurrentFrameIndex);

        frame.ResetCommandBuffer();

        const VkCommandBuffer commandBuffer = frame.GetCommandBuffer();
        RecordCommandBuffer(commandBuffer, imageIndex, m_CurrentFrameIndex);

        frame.ResetFence();

        const VkSemaphore imageAvailableSemaphores[] = {
            frame.GetImageAvailableSemaphore()
        };

        const VkSemaphore renderFinishedSemaphores[] = {
            m_Swapchain->GetRenderFinishedSemaphore(imageIndex)
        };

        const VkPipelineStageFlags waitStages[] = {
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
        };

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = imageAvailableSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = renderFinishedSemaphores;

        if (vkQueueSubmit(m_Device->GetGraphicsQueue(), 1, &submitInfo, frame.GetInFlightFence()) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to submit command buffer!");
        }

        const VkResult presentResult = m_Swapchain->Present(imageIndex);

        if (presentResult != VK_SUCCESS && presentResult != VK_SUBOPTIMAL_KHR && presentResult != VK_ERROR_OUT_OF_DATE_KHR)
        {
            throw std::runtime_error("Failed to present swapchain image!");
        }

        const bool shouldRecreate = 
            acquireResult == VK_SUBOPTIMAL_KHR ||
            presentResult == VK_SUBOPTIMAL_KHR ||
            presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
            m_Window.WasFramebufferResized();

        if (shouldRecreate)
        {
            m_Window.ResetFramebufferResized();
            RecreateSwapchain();
        }

        m_CurrentFrameIndex = (m_CurrentFrameIndex + 1) % MaxFramesInFlight;
    }

    void VulkanContext::WaitIdle()
    {
        if (m_Device)
        {
            m_Device->WaitIdle();
        }
    }
}