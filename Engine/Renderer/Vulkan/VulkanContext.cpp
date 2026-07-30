#include "Renderer/Vulkan/VulkanContext.h"
#include "Core/Window.h"
#include "Renderer/Vulkan/VulkanInstance.h"
#include "Renderer/Vulkan/VulkanSurface.h"
#include "Renderer/Vulkan/VulkanDevice.h"
#include "Renderer/Vulkan/VulkanSwapchain.h"
#include "Renderer/Vulkan/VulkanGraphicsPipeline.h"
#include "Renderer/Vulkan/VulkanGraphicsPipelineDescription.h"
#include "Renderer/Vertex.h"
#include "Renderer/Vulkan/VulkanFrameContext.h"
#include "Renderer/CameraUniform.h"
#include "Renderer/Vulkan/VulkanDescriptorSetLayout.h"
#include "Renderer/Vulkan/VulkanDescriptorPool.h"
#include "Renderer/Vulkan/VulkanDescriptorWriter.h"
#include "Renderer/Vulkan/VulkanBuffer.h"
#include "Renderer/Vulkan/VulkanRenderTarget.h"
#include "Renderer/Vulkan/VulkanRenderTargetDescription.h"
#include "Renderer/Vulkan/VulkanFullscreenPass.h"
#include "Renderer/Vulkan/VulkanImage.h"
#include "Renderer/Vulkan/VulkanDirectionalShadowPass.h"
#include "Renderer/ObjectPushConstant.h"
#include "Renderer/Mesh.h"
#include "Renderer/Vulkan/VulkanMesh.h"
#include "Renderer/Texture.h"
#include "Renderer/Vulkan/VulkanTexture.h"
#include "Renderer/Material.h"
#include "Renderer/MaterialUniform.h"
#include "Renderer/Vulkan/VulkanMaterial.h"
#include "Renderer/LightingUniform.h"
#include "Renderer/CubeTexture.h"
#include "Renderer/Vulkan/VulkanCubeTexture.h"
#include "Renderer/Vulkan/VulkanSkyboxPass.h"
#include "Renderer/EnvironmentLighting.h"
#include "Renderer/CubeTexture.h"
#include "Renderer/BrdfLutGenerator.h"
#include "Renderer/Vulkan/VulkanEnvironmentPrefilter.h"
#include "Renderer/Vulkan/VulkanBloomPass.h"
#include "Renderer/Vulkan/VulkanAutoExposurePass.h"
#include "Scene/Light.h"
#include "Scene/Scene.h"
#include "Scene/Camera.h"

#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <memory>
#include <stdexcept>
#include <utility>
#include <array>
#include <unordered_set>
#include <cmath>

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

        std::vector<VkImageView> sceneColorImageViews;
        sceneColorImageViews.reserve(MaxFramesInFlight);

        for (std::unique_ptr<VulkanRenderTarget>& renderTarget : m_SceneRenderTargets)
        {
            renderTarget = CreateSceneRenderTarget(m_Swapchain->GetExtent());
            sceneColorImageViews.push_back(renderTarget->GetColorImage(0).GetImageView());
        }

        m_ScenePipeline = CreateForwardPipeline(m_SceneRenderTargets.front()->GetRenderPass(), m_SceneRenderTargets.front()->GetExtent());
        m_SkyboxPass = std::make_unique<VulkanSkyboxPass>(*m_Device, m_SceneRenderTargets.front()->GetRenderPass(), m_SceneRenderTargets.front()->GetExtent(), m_GlobalDescriptorSetLayout->GetHandle());
        m_AutoExposurePass = std::make_unique<VulkanAutoExposurePass>(*m_Device, m_Swapchain->GetExtent(), sceneColorImageViews);
        m_BloomPass = std::make_unique<VulkanBloomPass>(*m_Device, m_Swapchain->GetExtent(), sceneColorImageViews);

        std::vector<VkImageView> bloomImageViews;
        std::vector<VkImageView> luminanceStatisticsImageViews;
        bloomImageViews.reserve(MaxFramesInFlight);
        luminanceStatisticsImageViews.reserve(MaxFramesInFlight);

        for (uint32_t frameIndex = 0; frameIndex < MaxFramesInFlight; ++frameIndex)
        {
            bloomImageViews.push_back(m_BloomPass->GetBloomImageView(frameIndex));
            luminanceStatisticsImageViews.push_back(m_AutoExposurePass->GetLuminanceStatisticsImageView(frameIndex));
        }

        m_FullscreenPass = std::make_unique<VulkanFullscreenPass>(*m_Device, m_Swapchain->GetRenderPass(), m_Swapchain->GetExtent(), sceneColorImageViews, bloomImageViews, luminanceStatisticsImageViews);

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
                if (!m_Textures.contains(texture)) m_Textures.emplace(texture, std::make_unique<VulkanTexture>(*m_Device, *texture));
            }
        }

        const std::shared_ptr<CubeTexture>& environment = m_Scene.GetEnvironment();

        if (!environment)
        {
            throw std::runtime_error("Scene does not have an environment texture!");
        }

        m_EnvironmentTexture = std::make_unique<VulkanCubeTexture>(*m_Device, *environment);
        m_PrefilteredEnvironment = std::make_unique<VulkanEnvironmentPrefilter>(*m_Device, *m_EnvironmentTexture, environment->GetWidth(), EnvironmentPrefilterResolution, EnvironmentPrefilterSampleCount);

        const std::shared_ptr<Texture> brdfLut = BrdfLutGenerator::Generate(256, 256);
        m_BrdfLutTexture = std::make_unique<VulkanTexture>(*m_Device, *brdfLut);
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
            *m_Device,
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

        m_MaterialDescriptorSetLayout = std::make_unique<VulkanDescriptorSetLayout>(
            *m_Device,
            std::vector<VkDescriptorSetLayoutBinding>{materialBinding, baseColorTextureBinding, ormTextureBinding, normalTextureBinding});

        for (std::unique_ptr<VulkanBuffer>& uniformBuffer : m_CameraUniformBuffers)
        {
            uniformBuffer = std::make_unique<VulkanBuffer>(*m_Device, sizeof(CameraUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
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

        m_LightingUniformBuffer = std::make_unique<VulkanBuffer>(*m_Device, sizeof(LightingUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        m_LightingUniformBuffer->Write(&lightingUniform, sizeof(lightingUniform));

        m_DirectionalShadowPass = std::make_unique<VulkanDirectionalShadowPass>(*m_Device, m_Scene, m_Meshes, m_GlobalDescriptorSetLayout->GetHandle(), MaxFramesInFlight);

        VkDescriptorPoolSize globalUniformPoolSize{};
        globalUniformPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        globalUniformPoolSize.descriptorCount = MaxFramesInFlight * 2;

        VkDescriptorPoolSize globalImagePoolSize{};
        globalImagePoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        globalImagePoolSize.descriptorCount = MaxFramesInFlight * 4;

        m_GlobalDescriptorPool = std::make_unique<VulkanDescriptorPool>(*m_Device, MaxFramesInFlight, std::vector<VkDescriptorPoolSize>{globalUniformPoolSize, globalImagePoolSize});
        m_GlobalDescriptorSets = m_GlobalDescriptorPool->AllocateSets(m_GlobalDescriptorSetLayout->GetHandle(), MaxFramesInFlight);

        VulkanDescriptorWriter writer(*m_Device);

        for (uint32_t frameIndex = 0; frameIndex < MaxFramesInFlight; ++frameIndex)
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

        m_MaterialDescriptorPool = std::make_unique<VulkanDescriptorPool>(*m_Device, materialCount, std::vector<VkDescriptorPoolSize>{materialUniformPoolSize, texturePoolSize});

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
                *m_Device,
                *material,
                *baseColorTextureIterator->second,
                *ormTextureIterator->second,
                *normalTextureIterator->second,
                *m_MaterialDescriptorPool,
                m_MaterialDescriptorSetLayout->GetHandle()));
        }
    }

    std::unique_ptr<VulkanRenderTarget> VulkanContext::CreateSceneRenderTarget(VkExtent2D extent)
    {
        VulkanRenderTargetDescription description{};
        description.extent = extent;

        VulkanRenderTargetColorAttachmentDescription colorAttachment{};
        colorAttachment.format = SceneColorFormat;
        colorAttachment.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        description.colorAttachments.push_back(colorAttachment);

        VulkanRenderTargetDepthAttachmentDescription depthAttachment{};
        depthAttachment.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        description.depthAttachment = depthAttachment;

        return std::make_unique<VulkanRenderTarget>(*m_Device, description);
    }

    std::unique_ptr<VulkanGraphicsPipeline> VulkanContext::CreateForwardPipeline(VkRenderPass renderPass, VkExtent2D extent)
    {
        VulkanGraphicsPipelineDescription description{};
        description.vertexShaderPath = std::filesystem::path(KOSMOS_SHADER_DIR) / "ForwardLit.vert.spv";
        description.fragmentShaderPath = std::filesystem::path(KOSMOS_SHADER_DIR) / "ForwardLit.frag.spv";
        description.renderPass = renderPass;
        description.extent = extent;
        description.descriptorSetLayouts = {
            m_GlobalDescriptorSetLayout->GetHandle(),
            m_MaterialDescriptorSetLayout->GetHandle()
        };

        VkPushConstantRange objectPushConstantRange{};
        objectPushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        objectPushConstantRange.offset = 0;
        objectPushConstantRange.size = sizeof(ObjectPushConstant);
        description.pushConstantRanges.push_back(objectPushConstantRange);

        description.vertexBindings.push_back({
            0,
            static_cast<uint32_t>(sizeof(Vertex)),
            VK_VERTEX_INPUT_RATE_VERTEX
        });

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
        description.colorBlendAttachments.push_back(colorBlendAttachment);

        description.cullMode = VK_CULL_MODE_BACK_BIT;
        description.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        description.useDepthStencil = true;
        description.depthTestEnable = VK_TRUE;
        description.depthWriteEnable = VK_TRUE;
        description.depthCompareOp = VK_COMPARE_OP_LESS;

        return std::make_unique<VulkanGraphicsPipeline>(*m_Device, description);
    }

    void VulkanContext::RecordSceneCommands(VkCommandBuffer commandBuffer, VulkanGraphicsPipeline& pipeline, uint32_t frameIndex)
    {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetHandle());

        const VkDescriptorSet globalDescriptorSet = m_GlobalDescriptorSets[frameIndex];
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

    void VulkanContext::UpdateCameraUniform(uint32_t frameIndex)
    {
        const VkExtent2D extent = m_Swapchain->GetExtent();
        const float aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);

        CameraUniform cameraUniform{};
        cameraUniform.view = m_Camera.GetViewMatrix();
        cameraUniform.projection = m_Camera.GetProjectionMatrix(aspectRatio);
        cameraUniform.projection[1][1] *= -1.0f;
        cameraUniform.position = glm::vec4(m_Camera.GetPosition(), 1.0f);

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

        std::array<std::unique_ptr<VulkanRenderTarget>, MaxFramesInFlight> newSceneRenderTargets;
        std::vector<VkImageView> sceneColorImageViews;
        sceneColorImageViews.reserve(MaxFramesInFlight);

        for (std::unique_ptr<VulkanRenderTarget>& renderTarget : newSceneRenderTargets)
        {
            renderTarget = CreateSceneRenderTarget(newSwapchain->GetExtent());
            sceneColorImageViews.push_back(renderTarget->GetColorImage(0).GetImageView());
        }

        auto newScenePipeline = CreateForwardPipeline(newSceneRenderTargets.front()->GetRenderPass(), newSceneRenderTargets.front()->GetExtent());
        auto newSkyboxPass = std::make_unique<VulkanSkyboxPass>(
            *m_Device,
            newSceneRenderTargets.front()->GetRenderPass(),
            newSceneRenderTargets.front()->GetExtent(),
            m_GlobalDescriptorSetLayout->GetHandle());

        auto newAutoExposurePass = std::make_unique<VulkanAutoExposurePass>(*m_Device, newSwapchain->GetExtent(), sceneColorImageViews);
        auto newBloomPass = std::make_unique<VulkanBloomPass>(*m_Device, newSwapchain->GetExtent(), sceneColorImageViews);

        std::vector<VkImageView> bloomImageViews;
        std::vector<VkImageView> luminanceStatisticsImageViews;
        bloomImageViews.reserve(MaxFramesInFlight);
        luminanceStatisticsImageViews.reserve(MaxFramesInFlight);

        for (uint32_t frameIndex = 0; frameIndex < MaxFramesInFlight; ++frameIndex)
        {
            bloomImageViews.push_back(newBloomPass->GetBloomImageView(frameIndex));
            luminanceStatisticsImageViews.push_back(newAutoExposurePass->GetLuminanceStatisticsImageView(frameIndex));
        }

        auto newFullscreenPass = std::make_unique<VulkanFullscreenPass>(*m_Device, newSwapchain->GetRenderPass(), newSwapchain->GetExtent(), sceneColorImageViews, bloomImageViews, luminanceStatisticsImageViews);

        m_Device->WaitIdle();
        m_FullscreenPass = std::move(newFullscreenPass);
        m_BloomPass = std::move(newBloomPass);
        m_AutoExposurePass = std::move(newAutoExposurePass);
        m_SkyboxPass = std::move(newSkyboxPass);
        m_ScenePipeline = std::move(newScenePipeline);
        m_SceneRenderTargets = std::move(newSceneRenderTargets);
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

        m_DirectionalShadowPass->Record(commandBuffer, frameIndex, m_GlobalDescriptorSets[frameIndex]);

        VulkanRenderTarget& sceneRenderTarget = *m_SceneRenderTargets[frameIndex];

        std::array<VkClearValue, 2> sceneClearValues{};
        sceneClearValues[0].color = {{0.02f, 0.03f, 0.04f, 1.0f}};
        sceneClearValues[1].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo sceneRenderPassInfo{};
        sceneRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        sceneRenderPassInfo.renderPass = sceneRenderTarget.GetRenderPass();
        sceneRenderPassInfo.framebuffer = sceneRenderTarget.GetFramebuffer();
        sceneRenderPassInfo.renderArea.offset = {0, 0};
        sceneRenderPassInfo.renderArea.extent = sceneRenderTarget.GetExtent();
        sceneRenderPassInfo.clearValueCount = static_cast<uint32_t>(sceneClearValues.size());
        sceneRenderPassInfo.pClearValues = sceneClearValues.data();

        vkCmdBeginRenderPass(commandBuffer, &sceneRenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        m_SkyboxPass->Record(commandBuffer, m_GlobalDescriptorSets[frameIndex]);
        RecordSceneCommands(commandBuffer, *m_ScenePipeline, frameIndex);
        vkCmdEndRenderPass(commandBuffer);

        VkClearValue swapchainClearValue{};
        swapchainClearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

        VkRenderPassBeginInfo swapchainRenderPassInfo{};
        swapchainRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        swapchainRenderPassInfo.renderPass = m_Swapchain->GetRenderPass();
        swapchainRenderPassInfo.framebuffer = m_Swapchain->GetFramebuffer(imageIndex);
        swapchainRenderPassInfo.renderArea.offset = {0, 0};
        swapchainRenderPassInfo.renderArea.extent = m_Swapchain->GetExtent();
        swapchainRenderPassInfo.clearValueCount = 1;
        swapchainRenderPassInfo.pClearValues = &swapchainClearValue;

        m_AutoExposurePass->Record(commandBuffer, frameIndex);
        m_BloomPass->Record(commandBuffer, frameIndex, BloomThreshold, BloomKnee);

        vkCmdBeginRenderPass(commandBuffer, &swapchainRenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        m_FullscreenPass->Record(commandBuffer, frameIndex, m_Exposure, BloomIntensity, MinimumAutomaticExposure, MaximumAutomaticExposure);
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

    void VulkanContext::SetExposure(float exposure)
    {
        if (!std::isfinite(exposure) || exposure < 0.0f)
        {
            throw std::invalid_argument("Exposure must be a finite non-negative value!");
        }

        m_Exposure = exposure;
    }
}