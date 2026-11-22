#include "Editor.h"
#include "Model.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <iostream>
#include <iterator>
#include <stdexcept>

#ifdef _WIN32
#define NOMINMAX
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <commdlg.h>
#endif

namespace fs = std::filesystem;

namespace
{
fs::path createImportDirectory()
{
    const auto temp = fs::temp_directory_path();
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto prefix = "KosmosEditor-" + std::to_string(stamp) + '-';
    for (unsigned suffix = 0;; ++suffix)
    {
        auto path = temp / (prefix + std::to_string(suffix));
        if (fs::create_directory(path))
            return path;
    }
}

fs::path chooseModel(GLFWwindow* window)
{
#ifdef _WIN32
    wchar_t filename[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = glfwGetWin32Window(window);
    dialog.lpstrFile = filename;
    dialog.nMaxFile = static_cast<DWORD>(std::size(filename));
    dialog.lpstrTitle = L"Import Model";
    dialog.lpstrFilter = L"glTF models (*.gltf)\0*.gltf\0";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog))
        return filename;
    if (const auto error = CommDlgExtendedError())
        throw std::runtime_error("Cannot open file picker (error " + std::to_string(error) + ")");
    return {};
#else
    (void)window;
    throw std::runtime_error("The model file picker requires Windows.");
#endif
}

bool beginPanel(const char* title, ImVec2 position, ImVec2 size)
{
    ImGui::SetNextWindowPos(position);
    ImGui::SetNextWindowSize(size);
    return ImGui::Begin(title, nullptr,
                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
}
} // namespace

Editor::Editor(GLFWwindow* window, VulkanRenderer& renderer) : window_(window), renderer_(renderer)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    try
    {
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::GetIO().Fonts->AddFontDefaultVector();
        if (!ImGui_ImplGlfw_InitForVulkan(window_, true))
            throw std::runtime_error("Initialize ImGui GLFW backend");
        syncOverlay();
    }
    catch (...)
    {
        destroyOverlay();
        throw;
    }
}

Editor::~Editor()
{
    if (!importDirectory_.empty())
    {
        std::error_code error;
        fs::remove_all(importDirectory_, error);
        if (error)
            std::cerr << "Could not clean temporary imports: " << error.message() << '\n';
    }
    destroyOverlay();
}

void Editor::destroyOverlay()
{
    vkDeviceWaitIdle(renderer_.overlayTarget().device);
    const auto& io = ImGui::GetIO();
    if (io.BackendRendererUserData)
        ImGui_ImplVulkan_Shutdown();
    if (io.BackendPlatformUserData)
        ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Editor::syncOverlay()
{
    const auto target = renderer_.overlayTarget();
    if (overlay_.format == target.format && overlay_.imageCount == target.imageCount)
        return;
    if (ImGui::GetIO().BackendRendererUserData)
    {
        if (vkDeviceWaitIdle(target.device) != VK_SUCCESS)
            throw std::runtime_error("Wait for editor overlay");
        ImGui_ImplVulkan_Shutdown();
    }
    overlay_ = target;
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = target.instance;
    info.PhysicalDevice = target.physicalDevice;
    info.Device = target.device;
    info.QueueFamily = target.queueFamily;
    info.Queue = target.queue;
    info.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE;
    info.MinImageCount = 2;
    info.ImageCount = target.imageCount;
    info.UseDynamicRendering = true;
    auto& dynamic = info.PipelineInfoMain.PipelineRenderingCreateInfo;
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    dynamic.colorAttachmentCount = 1;
    dynamic.pColorAttachmentFormats = &overlay_.format;
    if (!ImGui_ImplVulkan_Init(&info))
        throw std::runtime_error("Initialize ImGui Vulkan backend");
}

float Editor::updateStyle()
{
    float scale = 1.0f;
    glfwGetWindowContentScale(window_, &scale, nullptr);
    scale = std::max(scale, 1.0f);
    if (scale == appliedScale_)
        return scale;

    ImGuiStyle style;
    ImGui::StyleColorsDark(&style);
    style.FontSizeBase = 16.0f;
    style.FontScaleDpi = scale;
    style.WindowPadding = {12.0f, 10.0f};
    style.FramePadding = {8.0f, 5.0f};
    style.ItemSpacing = {8.0f, 7.0f};
    style.WindowRounding = 5.0f;
    style.FrameRounding = 4.0f;
    style.Colors[ImGuiCol_WindowBg] = {0.019f, 0.022f, 0.029f, 1.0f};
    style.Colors[ImGuiCol_TitleBg] = {0.030f, 0.036f, 0.048f, 1.0f};
    style.Colors[ImGuiCol_TitleBgActive] = style.Colors[ImGuiCol_TitleBg];
    style.Colors[ImGuiCol_Border] = {0.055f, 0.065f, 0.085f, 1.0f};
    style.Colors[ImGuiCol_FrameBg] = {0.034f, 0.044f, 0.060f, 1.0f};
    style.Colors[ImGuiCol_Button] = {0.052f, 0.073f, 0.105f, 1.0f};
    style.Colors[ImGuiCol_Header] = style.Colors[ImGuiCol_Button];
    style.ScaleAllSizes(scale);
    ImGui::GetStyle() = style;
    appliedScale_ = scale;
    return scale;
}

glm::mat4 Editor::Object::transform() const
{
    auto result = glm::translate(glm::mat4(1.0f), position);
    result = glm::rotate(result, glm::radians(rotation.z), glm::vec3(0, 0, 1));
    result = glm::rotate(result, glm::radians(rotation.y), glm::vec3(0, 1, 0));
    result = glm::rotate(result, glm::radians(rotation.x), glm::vec3(1, 0, 0));
    return glm::scale(result, scale);
}

void Editor::importModel()
{
    fs::path imported;
    try
    {
        const auto source = chooseModel(window_);
        if (source.empty())
            return;
        if (importDirectory_.empty())
            importDirectory_ = createImportDirectory();
        imported = copyModel(source, importDirectory_);
        assets_.reserve(assets_.size() + 1);
        assets_.push_back(
            {pathUtf8(imported.parent_path().filename()), renderer_.uploadModel(imported)});
        selectedAsset_ = assets_.size() - 1;
        filter_[0] = '\0';
        error_.clear();
    }
    catch (const std::exception& error)
    {
        if (!imported.empty())
        {
            std::error_code ignored;
            fs::remove_all(imported.parent_path(), ignored);
        }
        error_ = error.what();
    }
}

void Editor::addObject(size_t asset)
{
    try
    {
        objects_.push_back({asset});
        selectedObject_ = objects_.size() - 1;
        error_.clear();
    }
    catch (const std::exception& error)
    {
        error_ = error.what();
    }
}

void Editor::drawScene()
{
    std::optional<size_t> objectToDelete;
    if (objects_.empty())
        ImGui::TextDisabled("No scene objects");
    for (size_t i = 0; i < objects_.size(); ++i)
    {
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(assets_[objects_[i].asset].name.c_str(), selectedObject_ == i))
            selectedObject_ = i;
        if (ImGui::BeginPopupContextItem())
        {
            selectedObject_ = i;
            if (ImGui::MenuItem("Delete", "Del"))
                objectToDelete = i;
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (selectedObject_ && ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive() &&
        ImGui::IsKeyPressed(ImGuiKey_Delete, false))
        objectToDelete = selectedObject_;
    if (objectToDelete)
    {
        objects_.erase(objects_.begin() + *objectToDelete);
        selectedObject_.reset();
    }
}

void Editor::drawAssets()
{
    const auto& style = ImGui::GetStyle();
    const float rightEdge = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    if (ImGui::Button("Import Model..."))
        importModel();
    const float addWidth = ImGui::CalcTextSize("Add to Scene").x + style.FramePadding.x * 2.0f;
    if (rightEdge - ImGui::GetItemRectMax().x > style.ItemSpacing.x + addWidth)
        ImGui::SameLine();
    ImGui::BeginDisabled(!selectedAsset_);
    if (ImGui::Button("Add to Scene"))
        addObject(*selectedAsset_);
    ImGui::EndDisabled();
    const float filterWidth = ImGui::CalcTextSize("Filter assets").x + style.FramePadding.x * 4.0f;
    if (rightEdge - ImGui::GetItemRectMax().x > style.ItemSpacing.x + filterWidth)
        ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##filter", "Filter assets", filter_, sizeof(filter_));
    if (!error_.empty())
        ImGui::TextWrapped("%s", error_.c_str());
    ImGui::Separator();

    ImGui::BeginChild("Asset list");
    bool any = false;
    for (size_t i = 0; i < assets_.size(); ++i)
    {
        const auto& asset = assets_[i];
        if (asset.name.find(filter_) == std::string::npos)
            continue;
        any = true;
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(asset.name.c_str(), selectedAsset_ == i,
                              ImGuiSelectableFlags_AllowDoubleClick))
        {
            selectedAsset_ = i;
            if (ImGui::IsMouseDoubleClicked(0))
                addObject(i);
        }
        ImGui::PopID();
    }
    if (!any)
        ImGui::TextDisabled(assets_.empty() ? "Import a .gltf model to get started."
                                            : "No matching assets");
    ImGui::EndChild();
}

void Editor::drawDetails()
{
    if (!selectedObject_)
        ImGui::TextDisabled("Select a scene object");
    else
    {
        auto& object = objects_[*selectedObject_];
        const auto& asset = assets_[object.asset];
        ImGui::TextUnformatted(asset.name.c_str());
        ImGui::SeparatorText("Transform");
        const float moveStep = std::clamp(asset.model.radius * 0.03f, 0.0001f, 0.2f);
        ImGui::PushItemWidth(-1.0f);
        ImGui::TextUnformatted("Position");
        ImGui::DragFloat3("##Position", &object.position.x, moveStep, 0.0f, 0.0f, "%.4f");
        ImGui::TextUnformatted("Rotation");
        ImGui::DragFloat3("##Rotation", &object.rotation.x, 0.5f, 0.0f, 0.0f, "%.1f");
        ImGui::TextUnformatted("Scale");
        ImGui::DragFloat3("##Scale", &object.scale.x, 0.01f, 0.001f, 1000.0f, "%.3f",
                          ImGuiSliderFlags_AlwaysClamp);
        ImGui::PopItemWidth();
    }
    if (ImGui::CollapsingHeader("Rendering"))
        drawRenderingSettings();
}

void Editor::frame(float deltaTime)
{
    syncOverlay();
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    const float uiScale = updateStyle();
    ImGui::NewFrame();
    const auto& io = ImGui::GetIO();
    const float width = std::max(io.DisplaySize.x, 1.0f);
    const float height = std::max(io.DisplaySize.y, 1.0f);
    const float gap = std::min(10.0f * uiScale, width * 0.025f);
    const float left =
        std::min(std::clamp(width * 0.18f, 175.0f * uiScale, 300.0f * uiScale), width * 0.24f);
    const float right =
        std::min(std::clamp(width * 0.23f, 225.0f * uiScale, 360.0f * uiScale), width * 0.32f);
    const float bottom =
        std::min(std::clamp(height * 0.25f, 135.0f * uiScale, 280.0f * uiScale), height * 0.46f);
    const float topHeight = std::max(height - bottom - gap * 3.0f, 1.0f);
    const ImVec2 viewStart(left + gap * 2.0f, gap);
    const ImVec2 viewSize(std::max(width - left - right - gap * 4.0f, 1.0f), topHeight);
    const ImVec2 viewEnd(viewStart.x + viewSize.x, viewStart.y + viewSize.y);
    const bool sceneWasEmpty = objects_.empty();
    if (beginPanel("Scene", {gap, gap}, {left, topHeight}))
        drawScene();
    ImGui::End();
    if (beginPanel("Details", {width - right - gap, gap}, {right, topHeight}))
        drawDetails();
    ImGui::End();
    if (beginPanel("Assets", {gap, topHeight + gap * 2.0f},
                   {std::max(width - gap * 2.0f, 1.0f), bottom}))
        drawAssets();
    ImGui::End();

    if (objects_.empty())
    {
        constexpr const char* hint = "Add a model from Assets";
        const auto size = ImGui::CalcTextSize(hint);
        if (size.x < viewSize.x)
            ImGui::GetBackgroundDrawList()->AddText({viewStart.x + (viewSize.x - size.x) * 0.5f,
                                                     viewStart.y + (viewSize.y - size.y) * 0.5f},
                                                    ImGui::GetColorU32(ImGuiCol_TextDisabled),
                                                    hint);
    }
    const auto scale = io.DisplayFramebufferScale;
    VkRect2D viewport{{int32_t(viewStart.x * scale.x), int32_t(viewStart.y * scale.y)},
                      {uint32_t(std::max(viewSize.x * scale.x, 1.0f)),
                       uint32_t(std::max(viewSize.y * scale.y, 1.0f))}};
    const bool viewportInput =
        camera_.looking() ||
        (ImGui::IsMouseHoveringRect(viewStart, viewEnd, false) && !io.WantCaptureMouse);
    if (sceneWasEmpty && !objects_.empty())
    {
        const auto& model = assets_[objects_.front().asset].model;
        camera_.frameModel(model.center, model.radius,
                           float(viewport.extent.width) / float(viewport.extent.height));
    }
    camera_.update(window_, deltaTime, !viewportInput, !viewportInput || io.WantCaptureKeyboard);
    ImGui::Render();
    std::vector<ModelInstance> scene;
    scene.reserve(objects_.size());
    for (const auto& object : objects_)
        scene.push_back({assets_[object.asset].model, object.transform()});
    renderer_.draw(scene, camera_.view(), camera_.position(), camera_.nearPlane(), viewport,
                   [&](VkCommandBuffer command)
                   {
                       const auto target = renderer_.overlayTarget();
                       if (target.format == overlay_.format &&
                           target.imageCount == overlay_.imageCount)
                           ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command);
                   });
}

void Editor::drawRenderingSettings()
{
    auto& settings = renderer_.settings;
    const float fps = ImGui::GetIO().Framerate;
    ImGui::Text("%.1f FPS / %.2f ms", fps, 1000.0f / std::max(fps, 1.0f));
    ImGui::SliderFloat("Exposure", &settings.exposure, 0.05f, 8.0f, "%.2f",
                       ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
    ImGui::Checkbox("MSAA", &settings.msaa);
    ImGui::Checkbox("Environment background", &settings.environmentBackground);
    if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SliderFloat("Sun intensity", &settings.directionalIntensity, 0.0f, 10.0f, "%.2f",
                           ImGuiSliderFlags_AlwaysClamp);
        constexpr const char* lightNames = "Light 1\0Light 2\0Light 3\0Light 4\0";
        ImGui::Combo("Edit light", &selectedLight_, lightNames);
        auto& light = settings.lights[selectedLight_];
        ImGui::Checkbox("Enabled", &light.enabled);
        ImGui::BeginDisabled(!light.enabled);
        int type = light.spot ? 1 : 0;
        if (ImGui::Combo("Type", &type, "Point\0Spot\0"))
            light.spot = type == 1;
        ImGui::SliderFloat3("Position", &light.position.x, -5.0f, 5.0f, "%.2f",
                            ImGuiSliderFlags_AlwaysClamp);
        ImGui::ColorEdit3("Color", &light.color.x);
        ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 100.0f, "%.2f",
                           ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Range", &light.range, 0.1f, 10.0f, "%.2f",
                           ImGuiSliderFlags_AlwaysClamp);
        if (light.spot)
        {
            ImGui::SliderFloat("Yaw", &light.yaw, -180.0f, 180.0f, "%.1f deg",
                               ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("Pitch", &light.pitch, -90.0f, 90.0f, "%.1f deg",
                               ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("Inner cone", &light.innerAngle, 0.0f, light.outerAngle - 0.1f,
                               "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("Outer cone", &light.outerAngle, light.innerAngle + 0.1f, 90.0f,
                               "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
        }
        ImGui::EndDisabled();
        ImGui::Checkbox("Local shadows", &settings.localShadows);
        ImGui::BeginDisabled(!settings.localShadows);
        ImGui::Combo("Shadow light", &settings.shadowLight, lightNames);
        ImGui::SliderFloat("Shadow bias", &settings.localShadowBias, 0.0f, 0.05f, "%.4f",
                           ImGuiSliderFlags_AlwaysClamp);
        ImGui::Checkbox("Local PCF", &settings.localPcf);
        ImGui::BeginDisabled(!settings.localPcf);
        ImGui::SliderFloat("Filter radius", &settings.localFilterRadius, 0.0f, 4.0f, "%.2f",
                           ImGuiSliderFlags_AlwaysClamp);
        ImGui::EndDisabled();
        ImGui::EndDisabled();
    }
    ImGui::Separator();
    ImGui::Checkbox("Bloom", &settings.bloom);
    ImGui::BeginDisabled(!settings.bloom);
    ImGui::SliderFloat("Strength", &settings.bloomStrength, 0.0f, 1.0f, "%.3f",
                       ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Threshold", &settings.bloomThreshold, 0.0f, 10.0f, "%.3f",
                       ImGuiSliderFlags_AlwaysClamp);
    ImGui::EndDisabled();
    ImGui::Checkbox("Emissive", &settings.emissive);
    ImGui::BeginDisabled(!settings.emissive);
    ImGui::SliderFloat("Emissive multiplier", &settings.emissiveStrength, 0.0f, 20.0f, "%.2f",
                       ImGuiSliderFlags_AlwaysClamp);
    ImGui::EndDisabled();
    ImGui::Checkbox("Diffuse IBL", &settings.diffuseIbl);
    ImGui::BeginDisabled(!settings.diffuseIbl);
    ImGui::SliderFloat("Diffuse strength", &settings.diffuseIblStrength, 0.0f, 3.0f, "%.3f",
                       ImGuiSliderFlags_AlwaysClamp);
    ImGui::EndDisabled();
    ImGui::Checkbox("Specular IBL", &settings.specularIbl);
    ImGui::BeginDisabled(!settings.specularIbl);
    ImGui::SliderFloat("Specular strength", &settings.specularIblStrength, 0.0f, 3.0f, "%.3f",
                       ImGuiSliderFlags_AlwaysClamp);
    ImGui::EndDisabled();
    ImGui::Checkbox("Ambient occlusion", &settings.ambientOcclusion);
    ImGui::Checkbox("Shadows", &settings.shadows);
    ImGui::BeginDisabled(!settings.shadows);
    ImGui::SliderFloat("Depth bias", &settings.shadowBias, 0.0f, 0.005f, "%.5f",
                       ImGuiSliderFlags_AlwaysClamp);
    ImGui::Checkbox("Shadow PCF", &settings.shadowPcf);
    ImGui::EndDisabled();
    if (ImGui::Button("Reset rendering"))
    {
        settings = {};
        selectedLight_ = 0;
    }
}
