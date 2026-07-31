#include "Renderer/Vulkan/Scene/VulkanSceneRenderer.h"
#include "Renderer/RenderSettings.h"
#include "Renderer/CameraUniform.h"
#include "Renderer/LightingUniform.h"
#include "Renderer/ObjectPushConstant.h"
#include "Renderer/Mesh.h"
#include "Renderer/Texture.h"
#include "Renderer/Material.h"
#include "Renderer/CubeTexture.h"
#include "Renderer/EnvironmentLighting.h"
#include "Renderer/BrdfLutGenerator.h"
#include "Renderer/Vulkan/Core/VulkanDevice.h"
#include "Renderer/Vulkan/View/VulkanRenderView.h"
#include "Renderer/Vulkan/Pipelines/VulkanGraphicsPipeline.h"
#include "Renderer/Vulkan/Resources/VulkanBuffer.h"
#include "Renderer/Vulkan/Descriptors/VulkanDescriptorSetLayout.h"
#include "Renderer/Vulkan/Descriptors/VulkanDescriptorPool.h"
#include "Renderer/Vulkan/Descriptors/VulkanDescriptorWriter.h"
#include "Renderer/Vulkan/RenderTargets/VulkanRenderTarget.h"
#include "Renderer/Vulkan/Passes/VulkanFullscreenPass.h"
#include "Renderer/Vulkan/Passes/VulkanDirectionalShadowPass.h"
#include "Renderer/Vulkan/Resources/VulkanMesh.h"
#include "Renderer/Vulkan/Resources/VulkanTexture.h"
#include "Renderer/Vulkan/Resources/VulkanMaterial.h"
#include "Renderer/Vulkan/Resources/VulkanCubeTexture.h"
#include "Renderer/Vulkan/Passes/VulkanSkyboxPass.h"
#include "Renderer/Vulkan/Passes/VulkanEnvironmentPrefilter.h"
#include "Renderer/Vulkan/Passes/VulkanBloomPass.h"
#include "Renderer/Vulkan/Passes/VulkanAutoExposurePass.h"
#include "Renderer/Vulkan/RenderTargets/VulkanGBuffer.h"
#include "Renderer/Vulkan/Passes/VulkanDeferredLightingPass.h"
#include "Renderer/Vulkan/Passes/VulkanSSAOPass.h"
#include "Scene/Light.h"
#include "Scene/Scene.h"
#include "Scene/Camera.h"

#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
    glm::mat4 CreateDirectionalLightViewProjection(const Kosmos::DirectionalLight& light)
    {
        const glm::vec3 direction = glm::normalize(light.direction);
        const glm::vec3 worldUp = std::abs(glm::dot(direction, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 lightPosition = light.shadowCenter - direction * light.shadowDistance;

        const glm::mat4 view = glm::lookAt(lightPosition, light.shadowCenter, worldUp);
        glm::mat4 projection = glm::ortho(-light.shadowHalfExtent, light.shadowHalfExtent, -light.shadowHalfExtent, light.shadowHalfExtent, light.shadowNearPlane, light.shadowFarPlane);
        projection[1][1] *= -1.0f;
        return projection * view;
    }
}

namespace Kosmos
{
    VulkanSceneRenderer::VulkanSceneRenderer(VulkanDevice& device, const Camera& camera, const Scene& scene, const RenderSettings& settings, VkExtent2D extent, uint32_t frameCount)
        : m_Device(device), m_Camera(camera), m_Scene(scene), m_Settings(settings), m_FrameCount(frameCount)
    {
        if (m_FrameCount == 0)
        {
            throw std::runtime_error("Vulkan scene renderer requires at least one frame!");
        }

        CreateMeshResources();
        CreateTextureResources();
        CreateDescriptorResources();
        m_RenderView = std::make_unique<VulkanRenderView>(m_Device, extent, m_FrameCount, m_GlobalDescriptorSetLayout->GetHandle(), m_MaterialDescriptorSetLayout->GetHandle());
    }

    VulkanSceneRenderer::~VulkanSceneRenderer() = default;

    void VulkanSceneRenderer::CreateMeshResources()
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
                m_Meshes.emplace(mesh, std::make_unique<VulkanMesh>(m_Device, *mesh));
            }
        }
    }

    void VulkanSceneRenderer::CreateTextureResources()
    {
        for (const RenderObject& object : m_Scene.GetRenderObjects())
        {
            if (!object.material)
            {
                throw std::runtime_error("Scene contains a render object without a material!");
            }

            const std::shared_ptr<Texture>& baseColorTexture = object.material->GetBaseColorTexture();
            const std::shared_ptr<Texture>& ormTexture = object.material->GetOrmTexture();
            const std::shared_ptr<Texture>& normalTexture = object.material->GetNormalTexture();

            if (!baseColorTexture || !ormTexture || !normalTexture)
            {
                throw std::runtime_error("PBR material contains a null texture!");
            }

            const std::array<const Texture*, 3> textures = {baseColorTexture.get(), ormTexture.get(), normalTexture.get()};

            for (const Texture* texture : textures)
            {
                if (!m_Textures.contains(texture))
                {
                    m_Textures.emplace(texture, std::make_unique<VulkanTexture>(m_Device, *texture));
                }
            }
        }

        const std::shared_ptr<CubeTexture>& environment = m_Scene.GetEnvironment();

        if (!environment)
        {
            throw std::runtime_error("Scene does not have an environment texture!");
        }

        m_EnvironmentTexture = std::make_unique<VulkanCubeTexture>(m_Device, *environment);
        m_PrefilteredEnvironment = std::make_unique<VulkanEnvironmentPrefilter>(m_Device, *m_EnvironmentTexture, environment->GetWidth(), m_Settings.environment.prefilterResolution, m_Settings.environment.prefilterSampleCount);

        const std::shared_ptr<Texture> brdfLut = BrdfLutGenerator::Generate(256, 256);
        m_BrdfLutTexture = std::make_unique<VulkanTexture>(m_Device, *brdfLut);
    }

    void VulkanSceneRenderer::CreateDescriptorResources()
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
        cameraBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding lightingBinding{};
        lightingBinding.binding = 1;
        lightingBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        lightingBinding.descriptorCount = 1;
        lightingBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding shadowBinding{};
        shadowBinding.binding = 2;
        shadowBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        shadowBinding.descriptorCount = 1;
        shadowBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding environmentBinding{};
        environmentBinding.binding = 3;
        environmentBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        environmentBinding.descriptorCount = 1;
        environmentBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding brdfLutBinding{};
        brdfLutBinding.binding = 4;
        brdfLutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        brdfLutBinding.descriptorCount = 1;
        brdfLutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding prefilteredEnvironmentBinding{};
        prefilteredEnvironmentBinding.binding = 5;
        prefilteredEnvironmentBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        prefilteredEnvironmentBinding.descriptorCount = 1;
        prefilteredEnvironmentBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        m_GlobalDescriptorSetLayout = std::make_unique<VulkanDescriptorSetLayout>(
            m_Device,
            std::vector<VkDescriptorSetLayoutBinding>{
                cameraBinding,
                lightingBinding,
                shadowBinding,
                environmentBinding,
                brdfLutBinding,
                prefilteredEnvironmentBinding
            });

        VkDescriptorSetLayoutBinding materialBinding{};
        materialBinding.binding = 0;
        materialBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        materialBinding.descriptorCount = 1;
        materialBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding baseColorTextureBinding{};
        baseColorTextureBinding.binding = 1;
        baseColorTextureBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        baseColorTextureBinding.descriptorCount = 1;
        baseColorTextureBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding ormTextureBinding{};
        ormTextureBinding.binding = 2;
        ormTextureBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ormTextureBinding.descriptorCount = 1;
        ormTextureBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding normalTextureBinding{};
        normalTextureBinding.binding = 3;
        normalTextureBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        normalTextureBinding.descriptorCount = 1;
        normalTextureBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        m_MaterialDescriptorSetLayout = std::make_unique<VulkanDescriptorSetLayout>(m_Device, std::vector<VkDescriptorSetLayoutBinding>{materialBinding, baseColorTextureBinding, ormTextureBinding, normalTextureBinding});

        m_CameraUniformBuffers.reserve(m_FrameCount);

        for (uint32_t frameIndex = 0; frameIndex < m_FrameCount; ++frameIndex)
        {
            m_CameraUniformBuffers.push_back(std::make_unique<VulkanBuffer>(m_Device, sizeof(CameraUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
        }

        const SceneLighting& sceneLighting = m_Scene.GetLighting();

        LightingUniform lightingUniform{};
        lightingUniform.directionalLightViewProjection = CreateDirectionalLightViewProjection(sceneLighting.directionalLight);
        lightingUniform.ambient = glm::vec4(sceneLighting.ambientColor, sceneLighting.ambientIntensity);
        lightingUniform.directionalDirection = glm::vec4(glm::normalize(sceneLighting.directionalLight.direction), 0.0f);
        lightingUniform.directionalColor = glm::vec4(sceneLighting.directionalLight.color, sceneLighting.directionalLight.intensity);
        lightingUniform.pointPosition = glm::vec4(sceneLighting.pointLight.position, 1.0f);
        lightingUniform.pointColor = glm::vec4(sceneLighting.pointLight.color, sceneLighting.pointLight.intensity);
        lightingUniform.pointAttenuation = glm::vec4(sceneLighting.pointLight.constantAttenuation, sceneLighting.pointLight.linearAttenuation, sceneLighting.pointLight.quadraticAttenuation, 0.0f);
        lightingUniform.directionalShadowParameters = glm::vec4(sceneLighting.directionalLight.shadowReceiverBias, sceneLighting.directionalLight.shadowNormalBias, sceneLighting.directionalLight.shadowStrength, sceneLighting.directionalLight.shadowFilterRadius);
        lightingUniform.environmentParameters = glm::vec4(static_cast<float>(m_PrefilteredEnvironment->GetMipLevels() - 1), 0.0f, 0.0f, 0.0f);
        lightingUniform.diffuseIrradianceSH = EnvironmentLighting::ProjectDiffuseIrradiance(*m_Scene.GetEnvironment());

        m_LightingUniformBuffer = std::make_unique<VulkanBuffer>(m_Device, sizeof(LightingUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        m_LightingUniformBuffer->Write(&lightingUniform, sizeof(lightingUniform));

        m_DirectionalShadowPass = std::make_unique<VulkanDirectionalShadowPass>(m_Device, m_Scene, m_Meshes, m_GlobalDescriptorSetLayout->GetHandle(), m_FrameCount);

        VkDescriptorPoolSize globalUniformPoolSize{};
        globalUniformPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        globalUniformPoolSize.descriptorCount = m_FrameCount * 2;

        VkDescriptorPoolSize globalImagePoolSize{};
        globalImagePoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        globalImagePoolSize.descriptorCount = m_FrameCount * 4;

        m_GlobalDescriptorPool = std::make_unique<VulkanDescriptorPool>(m_Device, m_FrameCount, std::vector<VkDescriptorPoolSize>{globalUniformPoolSize, globalImagePoolSize});
        m_GlobalDescriptorSets = m_GlobalDescriptorPool->AllocateSets(m_GlobalDescriptorSetLayout->GetHandle(), m_FrameCount);

        VulkanDescriptorWriter writer(m_Device);

        for (uint32_t frameIndex = 0; frameIndex < m_FrameCount; ++frameIndex)
        {
            writer.WriteBuffer(m_GlobalDescriptorSets[frameIndex], 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, m_CameraUniformBuffers[frameIndex]->GetHandle(), 0, sizeof(CameraUniform));
            writer.WriteBuffer(m_GlobalDescriptorSets[frameIndex], 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, m_LightingUniformBuffer->GetHandle(), 0, sizeof(LightingUniform));
            writer.WriteImage(m_GlobalDescriptorSets[frameIndex], 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_DirectionalShadowPass->GetShadowMapImageView(frameIndex), m_DirectionalShadowPass->GetSampler(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
            writer.WriteImage(m_GlobalDescriptorSets[frameIndex], 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_EnvironmentTexture->GetImageView(), m_EnvironmentTexture->GetSampler(), m_EnvironmentTexture->GetLayout());
            writer.WriteImage(m_GlobalDescriptorSets[frameIndex], 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_BrdfLutTexture->GetImageView(), m_BrdfLutTexture->GetSampler(), m_BrdfLutTexture->GetLayout());
            writer.WriteImage(m_GlobalDescriptorSets[frameIndex], 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_PrefilteredEnvironment->GetImageView(), m_PrefilteredEnvironment->GetSampler(), m_PrefilteredEnvironment->GetLayout());
        }

        const uint32_t materialCount = static_cast<uint32_t>(materialHandles.size());

        VkDescriptorPoolSize materialUniformPoolSize{};
        materialUniformPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        materialUniformPoolSize.descriptorCount = materialCount;

        VkDescriptorPoolSize texturePoolSize{};
        texturePoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        texturePoolSize.descriptorCount = materialCount * 3;

        m_MaterialDescriptorPool = std::make_unique<VulkanDescriptorPool>(m_Device, materialCount, std::vector<VkDescriptorPoolSize>{materialUniformPoolSize, texturePoolSize});

        for (const Material* material : materialHandles)
        {
            const std::shared_ptr<Texture>& baseColorTexture = material->GetBaseColorTexture();
            const std::shared_ptr<Texture>& ormTexture = material->GetOrmTexture();
            const std::shared_ptr<Texture>& normalTexture = material->GetNormalTexture();
            const auto baseColorTextureIterator = m_Textures.find(baseColorTexture.get());
            const auto ormTextureIterator = m_Textures.find(ormTexture.get());
            const auto normalTextureIterator = m_Textures.find(normalTexture.get());

            if (baseColorTextureIterator == m_Textures.end() || ormTextureIterator == m_Textures.end() || normalTextureIterator == m_Textures.end())
            {
                throw std::runtime_error("Material texture does not have a Vulkan texture resource!");
            }

            m_Materials.emplace(material, std::make_unique<VulkanMaterial>(
                m_Device,
                *material,
                *baseColorTextureIterator->second,
                *ormTextureIterator->second,
                *normalTextureIterator->second,
                *m_MaterialDescriptorPool,
                m_MaterialDescriptorSetLayout->GetHandle()));
        }
    }

    void VulkanSceneRenderer::RecordSceneCommands(VkCommandBuffer commandBuffer, VulkanGraphicsPipeline& pipeline, uint32_t frameIndex)
    {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetHandle());

        const VkDescriptorSet globalDescriptorSet = m_GlobalDescriptorSets.at(frameIndex);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetLayout(), 0, 1, &globalDescriptorSet, 0, nullptr);

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
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetLayout(), 1, 1, &materialDescriptorSet, 0, nullptr);
                boundMaterial = &material;
            }

            ObjectPushConstant objectPushConstant{};
            objectPushConstant.model = object.transform.GetMatrix();
            objectPushConstant.normalMatrix = glm::transpose(glm::inverse(objectPushConstant.model));
            vkCmdPushConstants(commandBuffer, pipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ObjectPushConstant), &objectPushConstant);
            mesh.Draw(commandBuffer);
        }
    }

    void VulkanSceneRenderer::UpdateCameraUniform(uint32_t frameIndex)
    {
        const VkExtent2D extent = m_RenderView->GetExtent();
        const float aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);

        CameraUniform cameraUniform{};
        cameraUniform.view = m_Camera.GetViewMatrix();
        cameraUniform.projection = m_Camera.GetProjectionMatrix(aspectRatio);
        cameraUniform.projection[1][1] *= -1.0f;
        cameraUniform.inverseViewProjection = glm::inverse(cameraUniform.projection * cameraUniform.view);
        cameraUniform.position = glm::vec4(m_Camera.GetPosition(), 1.0f);
        m_CameraUniformBuffers.at(frameIndex)->Write(&cameraUniform, sizeof(CameraUniform));
    }

    void VulkanSceneRenderer::RecreateView(VkExtent2D extent)
    {
        auto newRenderView = std::make_unique<VulkanRenderView>(m_Device, extent, m_FrameCount, m_GlobalDescriptorSetLayout->GetHandle(), m_MaterialDescriptorSetLayout->GetHandle());
        m_Device.WaitIdle();
        m_RenderView = std::move(newRenderView);
    }

    std::vector<VulkanRenderViewImages> VulkanSceneRenderer::GetRenderViewImages() const
    {
        std::vector<VulkanRenderViewImages> images;
        images.reserve(m_FrameCount);

        for (uint32_t frameIndex = 0; frameIndex < m_FrameCount; ++frameIndex)
        {
            images.push_back(m_RenderView->GetImages(frameIndex));
        }

        return images;
    }

    void VulkanSceneRenderer::RecordFrame(VkCommandBuffer commandBuffer, uint32_t frameIndex, float deltaTime)
    {
        UpdateCameraUniform(frameIndex);
        const VkDescriptorSet globalDescriptorSet = m_GlobalDescriptorSets.at(frameIndex);
        m_DirectionalShadowPass->Record(commandBuffer, frameIndex, globalDescriptorSet);

        VulkanGBuffer& gBuffer = m_RenderView->GetGBuffer(frameIndex);
        std::array<VkClearValue, VulkanGBuffer::ColorAttachmentCount + 1> gBufferClearValues{};
        gBufferClearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        gBufferClearValues[1].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        gBufferClearValues[2].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
        gBufferClearValues[3].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo gBufferRenderPassInfo{};
        gBufferRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        gBufferRenderPassInfo.renderPass = gBuffer.GetRenderPass();
        gBufferRenderPassInfo.framebuffer = gBuffer.GetFramebuffer();
        gBufferRenderPassInfo.renderArea.offset = {0, 0};
        gBufferRenderPassInfo.renderArea.extent = gBuffer.GetExtent();
        gBufferRenderPassInfo.clearValueCount = static_cast<uint32_t>(gBufferClearValues.size());
        gBufferRenderPassInfo.pClearValues = gBufferClearValues.data();

        vkCmdBeginRenderPass(commandBuffer, &gBufferRenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        RecordSceneCommands(commandBuffer, m_RenderView->GetGBufferPipeline(), frameIndex);
        vkCmdEndRenderPass(commandBuffer);

        m_RenderView->GetSSAOPass().Record(commandBuffer, frameIndex, globalDescriptorSet, m_Settings.ssao.radius, m_Settings.ssao.bias, m_Settings.ssao.power, m_Settings.ssao.depthSharpness, m_Settings.ssao.normalSharpness);

        VulkanRenderTarget& sceneRenderTarget = m_RenderView->GetSceneRenderTarget(frameIndex);
        VkClearValue sceneClearValue{};
        sceneClearValue.color = {{0.02f, 0.03f, 0.04f, 1.0f}};

        VkRenderPassBeginInfo sceneRenderPassInfo{};
        sceneRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        sceneRenderPassInfo.renderPass = sceneRenderTarget.GetRenderPass();
        sceneRenderPassInfo.framebuffer = sceneRenderTarget.GetFramebuffer();
        sceneRenderPassInfo.renderArea.offset = {0, 0};
        sceneRenderPassInfo.renderArea.extent = sceneRenderTarget.GetExtent();
        sceneRenderPassInfo.clearValueCount = 1;
        sceneRenderPassInfo.pClearValues = &sceneClearValue;

        vkCmdBeginRenderPass(commandBuffer, &sceneRenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        m_RenderView->GetSkyboxPass().Record(commandBuffer, globalDescriptorSet);
        m_RenderView->GetDeferredLightingPass().Record(commandBuffer, frameIndex, globalDescriptorSet);
        vkCmdEndRenderPass(commandBuffer);

        m_RenderView->GetAutoExposurePass().Record(commandBuffer, frameIndex, deltaTime, m_Settings.automaticExposure.increaseSpeed, m_Settings.automaticExposure.decreaseSpeed, m_Settings.automaticExposure.minimum, m_Settings.automaticExposure.maximum);
        m_RenderView->GetBloomPass().Record(commandBuffer, frameIndex, m_Settings.bloom.threshold, m_Settings.bloom.knee);

        VulkanRenderTarget& outputRenderTarget = m_RenderView->GetOutputRenderTarget(frameIndex);
        VkClearValue outputClearValue{};
        outputClearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

        VkRenderPassBeginInfo outputRenderPassInfo{};
        outputRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        outputRenderPassInfo.renderPass = outputRenderTarget.GetRenderPass();
        outputRenderPassInfo.framebuffer = outputRenderTarget.GetFramebuffer();
        outputRenderPassInfo.renderArea.offset = {0, 0};
        outputRenderPassInfo.renderArea.extent = outputRenderTarget.GetExtent();
        outputRenderPassInfo.clearValueCount = 1;
        outputRenderPassInfo.pClearValues = &outputClearValue;

        vkCmdBeginRenderPass(commandBuffer, &outputRenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        m_RenderView->GetFullscreenPass().Record(commandBuffer, frameIndex, m_Settings.exposureCompensation, m_Settings.bloom.intensity);
        vkCmdEndRenderPass(commandBuffer);
    }
}
