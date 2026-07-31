#include "Renderer/Vulkan/VulkanSceneRenderer.h"
#include "Renderer/RenderSettings.h"
#include "Renderer/Vertex.h"
#include "Renderer/CameraUniform.h"
#include "Renderer/LightingUniform.h"
#include "Renderer/ObjectPushConstant.h"
#include "Renderer/Mesh.h"
#include "Renderer/Texture.h"
#include "Renderer/Material.h"
#include "Renderer/CubeTexture.h"
#include "Renderer/EnvironmentLighting.h"
#include "Renderer/BrdfLutGenerator.h"
#include "Renderer/Vulkan/VulkanDevice.h"
#include "Renderer/Vulkan/VulkanGraphicsPipeline.h"
#include "Renderer/Vulkan/VulkanGraphicsPipelineDescription.h"
#include "Renderer/Vulkan/VulkanBuffer.h"
#include "Renderer/Vulkan/VulkanDescriptorSetLayout.h"
#include "Renderer/Vulkan/VulkanDescriptorPool.h"
#include "Renderer/Vulkan/VulkanDescriptorWriter.h"
#include "Renderer/Vulkan/VulkanRenderTarget.h"
#include "Renderer/Vulkan/VulkanRenderTargetDescription.h"
#include "Renderer/Vulkan/VulkanFullscreenPass.h"
#include "Renderer/Vulkan/VulkanImage.h"
#include "Renderer/Vulkan/VulkanDirectionalShadowPass.h"
#include "Renderer/Vulkan/VulkanMesh.h"
#include "Renderer/Vulkan/VulkanTexture.h"
#include "Renderer/Vulkan/VulkanMaterial.h"
#include "Renderer/Vulkan/VulkanCubeTexture.h"
#include "Renderer/Vulkan/VulkanSkyboxPass.h"
#include "Renderer/Vulkan/VulkanEnvironmentPrefilter.h"
#include "Renderer/Vulkan/VulkanBloomPass.h"
#include "Renderer/Vulkan/VulkanAutoExposurePass.h"
#include "Renderer/Vulkan/VulkanGBuffer.h"
#include "Renderer/Vulkan/VulkanDeferredLightingPass.h"
#include "Renderer/Vulkan/VulkanSSAOPass.h"
#include "Scene/Light.h"
#include "Scene/Scene.h"
#include "Scene/Camera.h"

#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
    constexpr VkFormat SceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

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
    struct VulkanSceneRenderer::ViewResources
    {
        VkRenderPass presentationRenderPass = VK_NULL_HANDLE;
        VkExtent2D extent{};
        std::vector<std::unique_ptr<VulkanGBuffer>> gBuffers;
        std::unique_ptr<VulkanGraphicsPipeline> gBufferPipeline;
        std::unique_ptr<VulkanSSAOPass> ssaoPass;
        std::vector<std::unique_ptr<VulkanRenderTarget>> sceneRenderTargets;
        std::unique_ptr<VulkanSkyboxPass> skyboxPass;
        std::unique_ptr<VulkanDeferredLightingPass> deferredLightingPass;
        std::unique_ptr<VulkanAutoExposurePass> autoExposurePass;
        std::unique_ptr<VulkanBloomPass> bloomPass;
        std::unique_ptr<VulkanFullscreenPass> fullscreenPass;
    };

    VulkanSceneRenderer::VulkanSceneRenderer(VulkanDevice& device, const Camera& camera, const Scene& scene, const RenderSettings& settings, VkRenderPass presentationRenderPass, VkExtent2D extent, uint32_t frameCount)
        : m_Device(device), m_Camera(camera), m_Scene(scene), m_Settings(settings), m_FrameCount(frameCount)
    {
        if (m_FrameCount == 0)
        {
            throw std::runtime_error("Vulkan scene renderer requires at least one frame!");
        }

        CreateMeshResources();
        CreateTextureResources();
        CreateDescriptorResources();
        m_ViewResources = CreateViewResources(presentationRenderPass, extent);
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

    std::unique_ptr<VulkanSceneRenderer::ViewResources> VulkanSceneRenderer::CreateViewResources(VkRenderPass presentationRenderPass, VkExtent2D extent)
    {
        if (presentationRenderPass == VK_NULL_HANDLE || extent.width == 0 || extent.height == 0)
        {
            throw std::runtime_error("Vulkan scene renderer requires valid view resources!");
        }

        auto resources = std::make_unique<ViewResources>();
        resources->presentationRenderPass = presentationRenderPass;
        resources->extent = extent;
        resources->gBuffers.reserve(m_FrameCount);
        resources->sceneRenderTargets.reserve(m_FrameCount);

        std::vector<VkImageView> sceneColorImageViews;
        std::vector<const VulkanGBuffer*> gBuffers;
        sceneColorImageViews.reserve(m_FrameCount);
        gBuffers.reserve(m_FrameCount);

        VulkanRenderTargetDescription sceneRenderTargetDescription{};
        sceneRenderTargetDescription.extent = extent;

        VulkanRenderTargetColorAttachmentDescription sceneColorAttachment{};
        sceneColorAttachment.format = SceneColorFormat;
        sceneColorAttachment.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        sceneColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        sceneColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        sceneColorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        sceneColorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sceneRenderTargetDescription.colorAttachments.push_back(sceneColorAttachment);

        for (uint32_t frameIndex = 0; frameIndex < m_FrameCount; ++frameIndex)
        {
            resources->gBuffers.push_back(std::make_unique<VulkanGBuffer>(m_Device, extent));
            resources->sceneRenderTargets.push_back(std::make_unique<VulkanRenderTarget>(m_Device, sceneRenderTargetDescription));
            sceneColorImageViews.push_back(resources->sceneRenderTargets.back()->GetColorImage(0).GetImageView());
            gBuffers.push_back(resources->gBuffers.back().get());
        }

        resources->gBufferPipeline = CreateGBufferPipeline(resources->gBuffers.front()->GetRenderPass(), extent);
        resources->ssaoPass = std::make_unique<VulkanSSAOPass>(m_Device, extent, m_GlobalDescriptorSetLayout->GetHandle(), gBuffers);

        std::vector<VkImageView> ambientOcclusionImageViews;
        ambientOcclusionImageViews.reserve(m_FrameCount);

        for (uint32_t frameIndex = 0; frameIndex < m_FrameCount; ++frameIndex)
        {
            ambientOcclusionImageViews.push_back(resources->ssaoPass->GetAmbientOcclusionImageView(frameIndex));
        }

        resources->skyboxPass = std::make_unique<VulkanSkyboxPass>(m_Device, resources->sceneRenderTargets.front()->GetRenderPass(), extent, m_GlobalDescriptorSetLayout->GetHandle());
        resources->deferredLightingPass = std::make_unique<VulkanDeferredLightingPass>(m_Device, resources->sceneRenderTargets.front()->GetRenderPass(), extent, m_GlobalDescriptorSetLayout->GetHandle(), gBuffers, ambientOcclusionImageViews);
        resources->autoExposurePass = std::make_unique<VulkanAutoExposurePass>(m_Device, extent, sceneColorImageViews);
        resources->bloomPass = std::make_unique<VulkanBloomPass>(m_Device, extent, sceneColorImageViews);

        std::vector<VkImageView> bloomImageViews;
        std::vector<VkImageView> exposureImageViews;
        bloomImageViews.reserve(m_FrameCount);
        exposureImageViews.reserve(m_FrameCount);

        for (uint32_t frameIndex = 0; frameIndex < m_FrameCount; ++frameIndex)
        {
            bloomImageViews.push_back(resources->bloomPass->GetBloomImageView(frameIndex));
            exposureImageViews.push_back(resources->autoExposurePass->GetExposureImageView(frameIndex));
        }

        resources->fullscreenPass = std::make_unique<VulkanFullscreenPass>(m_Device, presentationRenderPass, extent, sceneColorImageViews, bloomImageViews, exposureImageViews);
        return resources;
    }

    std::unique_ptr<VulkanGraphicsPipeline> VulkanSceneRenderer::CreateGBufferPipeline(VkRenderPass renderPass, VkExtent2D extent)
    {
        VulkanGraphicsPipelineDescription description{};
        description.vertexShaderPath = std::filesystem::path(KOSMOS_SHADER_DIR) / "GBuffer.vert.spv";
        description.fragmentShaderPath = std::filesystem::path(KOSMOS_SHADER_DIR) / "GBuffer.frag.spv";
        description.renderPass = renderPass;
        description.extent = extent;
        description.descriptorSetLayouts = {m_GlobalDescriptorSetLayout->GetHandle(), m_MaterialDescriptorSetLayout->GetHandle()};

        VkPushConstantRange objectPushConstantRange{};
        objectPushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        objectPushConstantRange.offset = 0;
        objectPushConstantRange.size = sizeof(ObjectPushConstant);
        description.pushConstantRanges.push_back(objectPushConstantRange);

        description.vertexBindings.push_back({0, static_cast<uint32_t>(sizeof(Vertex)), VK_VERTEX_INPUT_RATE_VERTEX});
        description.vertexAttributes = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, position))},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, color))},
            {2, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, textureCoordinate))},
            {3, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, normal))},
            {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex, tangent))}
        };

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.blendEnable = VK_FALSE;
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        description.colorBlendAttachments.resize(VulkanGBuffer::ColorAttachmentCount, colorBlendAttachment);
        description.cullMode = VK_CULL_MODE_BACK_BIT;
        description.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        description.useDepthStencil = true;
        description.depthTestEnable = VK_TRUE;
        description.depthWriteEnable = VK_TRUE;
        description.depthCompareOp = VK_COMPARE_OP_LESS;
        return std::make_unique<VulkanGraphicsPipeline>(m_Device, description);
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
        const float aspectRatio = static_cast<float>(m_ViewResources->extent.width) / static_cast<float>(m_ViewResources->extent.height);

        CameraUniform cameraUniform{};
        cameraUniform.view = m_Camera.GetViewMatrix();
        cameraUniform.projection = m_Camera.GetProjectionMatrix(aspectRatio);
        cameraUniform.projection[1][1] *= -1.0f;
        cameraUniform.inverseViewProjection = glm::inverse(cameraUniform.projection * cameraUniform.view);
        cameraUniform.position = glm::vec4(m_Camera.GetPosition(), 1.0f);
        m_CameraUniformBuffers.at(frameIndex)->Write(&cameraUniform, sizeof(CameraUniform));
    }

    void VulkanSceneRenderer::RecreateView(VkRenderPass presentationRenderPass, VkExtent2D extent)
    {
        std::unique_ptr<ViewResources> newViewResources = CreateViewResources(presentationRenderPass, extent);
        m_Device.WaitIdle();
        m_ViewResources = std::move(newViewResources);
    }

    void VulkanSceneRenderer::RecordFrame(VkCommandBuffer commandBuffer, VkFramebuffer presentationFramebuffer, uint32_t frameIndex, float deltaTime)
    {
        if (presentationFramebuffer == VK_NULL_HANDLE)
        {
            throw std::runtime_error("Vulkan scene renderer requires a presentation framebuffer!");
        }

        UpdateCameraUniform(frameIndex);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to begin command buffer!");
        }

        const VkDescriptorSet globalDescriptorSet = m_GlobalDescriptorSets.at(frameIndex);
        m_DirectionalShadowPass->Record(commandBuffer, frameIndex, globalDescriptorSet);

        VulkanGBuffer& gBuffer = *m_ViewResources->gBuffers.at(frameIndex);
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
        RecordSceneCommands(commandBuffer, *m_ViewResources->gBufferPipeline, frameIndex);
        vkCmdEndRenderPass(commandBuffer);

        m_ViewResources->ssaoPass->Record(commandBuffer, frameIndex, globalDescriptorSet, m_Settings.ssao.radius, m_Settings.ssao.bias, m_Settings.ssao.power, m_Settings.ssao.depthSharpness, m_Settings.ssao.normalSharpness);

        VulkanRenderTarget& sceneRenderTarget = *m_ViewResources->sceneRenderTargets.at(frameIndex);
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
        m_ViewResources->skyboxPass->Record(commandBuffer, globalDescriptorSet);
        m_ViewResources->deferredLightingPass->Record(commandBuffer, frameIndex, globalDescriptorSet);
        vkCmdEndRenderPass(commandBuffer);

        m_ViewResources->autoExposurePass->Record(commandBuffer, frameIndex, deltaTime, m_Settings.automaticExposure.increaseSpeed, m_Settings.automaticExposure.decreaseSpeed, m_Settings.automaticExposure.minimum, m_Settings.automaticExposure.maximum);
        m_ViewResources->bloomPass->Record(commandBuffer, frameIndex, m_Settings.bloom.threshold, m_Settings.bloom.knee);

        VkClearValue presentationClearValue{};
        presentationClearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

        VkRenderPassBeginInfo presentationRenderPassInfo{};
        presentationRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        presentationRenderPassInfo.renderPass = m_ViewResources->presentationRenderPass;
        presentationRenderPassInfo.framebuffer = presentationFramebuffer;
        presentationRenderPassInfo.renderArea.offset = {0, 0};
        presentationRenderPassInfo.renderArea.extent = m_ViewResources->extent;
        presentationRenderPassInfo.clearValueCount = 1;
        presentationRenderPassInfo.pClearValues = &presentationClearValue;

        vkCmdBeginRenderPass(commandBuffer, &presentationRenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        m_ViewResources->fullscreenPass->Record(commandBuffer, frameIndex, m_Settings.exposureCompensation, m_Settings.bloom.intensity);
        vkCmdEndRenderPass(commandBuffer);

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to record command buffer!");
        }
    }
}
