#include "Editor.h"
#include "Model.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <commdlg.h>
#endif

namespace fs = std::filesystem;

namespace
{
struct ImportCanceled
{
};

const char* importStageLabel(ImportStage stage)
{
    switch (stage)
    {
    case ImportStage::Copying:
        return "Copying source files";
    case ImportStage::Reading:
        return "Reading model data";
    case ImportStage::BuildingMeshes:
        return "Building meshes";
    case ImportStage::GeneratingTangents:
        return "Generating tangents";
    case ImportStage::ReadingAnimation:
        return "Reading animation clips";
    case ImportStage::UploadingGeometry:
        return "Uploading geometry";
    case ImportStage::UploadingTextures:
        return "Uploading textures";
    case ImportStage::Finalizing:
        return "Finalizing asset";
    }
    return "Working";
}

std::optional<float> hitBounds(const ViewportRay& ray, const ModelBounds& bounds,
                               const glm::mat4& transform)
{
    const auto inverse = glm::inverse(transform);
    const glm::vec3 origin(inverse * glm::vec4(ray.origin, 1.0f));
    // Keep the world-space ray parameter when comparing differently scaled objects.
    const glm::vec3 direction(inverse * glm::vec4(ray.direction, 0.0f));
    float entry = -std::numeric_limits<float>::infinity();
    float exit = std::numeric_limits<float>::infinity();
    for (int axis = 0; axis < 3; ++axis)
    {
        if (direction[axis] == 0.0f)
        {
            if (origin[axis] < bounds.minimum[axis] || origin[axis] > bounds.maximum[axis])
                return {};
            continue;
        }
        const float a = (bounds.minimum[axis] - origin[axis]) / direction[axis];
        const float b = (bounds.maximum[axis] - origin[axis]) / direction[axis];
        entry = std::max(entry, std::min(a, b));
        exit = std::min(exit, std::max(a, b));
        if (entry > exit || exit < ray.nearDistance)
            return {};
    }
    return entry >= ray.nearDistance ? entry : exit;
}

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
    dialog.lpstrFilter = L"glTF models (*.gltf, *.glb)\0*.gltf;*.glb\0";
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
                            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                            ImGuiWindowFlags_NoBringToFrontOnFocus);
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
    stopImport_.store(true, std::memory_order_relaxed);
    if (importJob_.valid())
        importJob_.wait();
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
    auto queueLock = renderer_.lockQueues();
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
    const auto translation = glm::translate(glm::mat4(1.0f), position);
    return glm::scale(translation * glm::mat4_cast(glm::quat(glm::radians(rotation))), scale);
}

void Editor::importModel()
{
    if (importJob_.valid())
        return;
    fs::path source;
    try
    {
        source = chooseModel(window_);
        if (source.empty())
            return;
        importName_ = pathUtf8(source.stem());
        if (importDirectory_.empty())
            importDirectory_ = createImportDirectory();
        assets_.reserve(assets_.size() + 1);
        error_.clear();
        stopImport_.store(false, std::memory_order_relaxed);
        importStage_.store(ImportStage::Copying, std::memory_order_relaxed);
        importNoticeUntil_ = 0.0;
        importJob_ = std::async(std::launch::async,
                                [this, source, directory = importDirectory_]
                                {
                                    const auto report = [&](ImportStage stage)
                                    {
                                        if (stopImport_.load(std::memory_order_relaxed))
                                            throw ImportCanceled{};
                                        importStage_.store(stage, std::memory_order_relaxed);
                                    };
                                    fs::path imported;
                                    try
                                    {
                                        imported = copyModel(source, directory, report);
                                        auto name = pathUtf8(imported.parent_path().filename());
                                        auto model = loadModel(imported, report);
                                        auto xmp = std::move(model.xmp);
                                        return Asset{std::move(name),
                                                     renderer_.uploadModel(std::move(model), report),
                                                     std::move(xmp)};
                                    }
                                    catch (...)
                                    {
                                        if (!imported.empty())
                                        {
                                            std::error_code ignored;
                                            fs::remove_all(imported.parent_path(), ignored);
                                        }
                                        throw;
                                    }
                                });
    }
    catch (const std::exception& error)
    {
        if (source.empty())
            importName_.clear();
        error_ = error.what();
        showImportResult("Import failed");
    }
}

void Editor::pollImport()
{
    if (!importJob_.valid() ||
        importJob_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    try
    {
        assets_.push_back(importJob_.get());
        selectedAsset_ = assets_.size() - 1;
        filter_[0] = '\0';
        showImportResult("Import complete");
    }
    catch (const ImportCanceled&)
    {
        showImportResult("Import canceled");
    }
    catch (const std::exception& error)
    {
        error_ = error.what();
        showImportResult("Import failed");
    }
}

void Editor::showImportResult(const char* result)
{
    importResult_ = result;
    importNoticeUntil_ = ImGui::GetTime() + 2.5;
}

void Editor::drawImportNotification(float right, float bottom, float scale)
{
    const bool importing = importJob_.valid();
    if (!importing && ImGui::GetTime() >= importNoticeUntil_)
        return;

    ImGui::SetNextWindowPos({right, bottom}, ImGuiCond_Always, {1.0f, 1.0f});
    ImGui::SetNextWindowSize({270.0f * scale, 0.0f});
    constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
                           ImGuiWindowFlags_NoFocusOnAppearing;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.045f, 0.058f, 0.077f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.15f, 0.21f, 0.30f, 1.0f));
    if (ImGui::Begin("##ImportNotification", nullptr, flags))
    {
        const bool stopping = importing && stopImport_.load(std::memory_order_relaxed);
        if (importing)
        {
            const float height = ImGui::GetFrameHeight();
            const auto cursor = ImGui::GetCursorScreenPos();
            const float angle = static_cast<float>(ImGui::GetTime()) * 5.0f;
            auto* draw = ImGui::GetWindowDrawList();
            draw->PathArcTo({cursor.x + 7.0f * scale, cursor.y + height * 0.5f}, 5.0f * scale,
                            angle, angle + 4.6f);
            draw->PathStroke(ImGui::GetColorU32(ImGuiCol_CheckMark), 0, 2.0f * scale);
            ImGui::Dummy({14.0f * scale, height});
            ImGui::SameLine();
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(importing ? (stopping ? "Canceling..." : "Importing (1)")
                                         : importResult_);
        if (importing)
        {
            const auto& style = ImGui::GetStyle();
            const float buttonWidth = ImGui::CalcTextSize("Cancel").x + style.FramePadding.x * 2.0f;
            ImGui::SameLine(ImGui::GetWindowWidth() - style.WindowPadding.x - buttonWidth);
            ImGui::BeginDisabled(stopping);
            if (ImGui::Button("Cancel"))
                stopImport_.store(true, std::memory_order_relaxed);
            ImGui::EndDisabled();
        }
        if (!importName_.empty())
            ImGui::TextWrapped("%s", importName_.c_str());
        if (importing && !stopping)
            ImGui::TextDisabled("%s",
                                importStageLabel(importStage_.load(std::memory_order_relaxed)));
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
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
    ImGui::BeginDisabled(importJob_.valid());
    if (ImGui::Button("Import Model..."))
        importModel();
    ImGui::EndDisabled();
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
    std::optional<size_t> metadataToShow;
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
        if (asset.xmp && ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem("Show Metadata"))
                metadataToShow = i;
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (!any)
        ImGui::TextDisabled(assets_.empty() ? "Import a .gltf or .glb model to get started."
                                            : "No matching assets");
    ImGui::EndChild();

    if (metadataToShow)
    {
        metadataAsset_ = *metadataToShow;
        ImGui::OpenPopup("Asset Metadata");
        const auto viewport = ImGui::GetMainViewport()->WorkSize;
        const float fontSize = ImGui::GetFontSize();
        ImGui::SetNextWindowSize({std::min(32.0f * fontSize, viewport.x),
                                  std::min(20.0f * fontSize, viewport.y)},
                                 ImGuiCond_Appearing);
    }
    if (ImGui::BeginPopupModal("Asset Metadata", nullptr, ImGuiWindowFlags_NoSavedSettings))
    {
        const auto& asset = assets_[metadataAsset_];
        const auto& metadata = *asset.xmp;
        ImGui::TextUnformatted(asset.name.c_str());
        ImGui::Separator();
        ImGui::BeginChild("Metadata fields", {0.0f, -ImGui::GetFrameHeightWithSpacing()});
        const std::pair<const char*, const std::string&> fields[]{
            {"Title", metadata.title}, {"Author", metadata.author},
            {"Description", metadata.description}, {"Rights", metadata.rights},
            {"License", metadata.license}};
        for (const auto& [label, value] : fields)
            if (!value.empty())
            {
                ImGui::TextDisabled("%s", label);
                ImGui::TextWrapped("%s", value.c_str());
            }
        ImGui::EndChild();
        if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
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
        const auto& variants = asset.model.scene->materialVariants;
        if (!variants.empty())
        {
            const auto variant = object.materialVariant;
            const char* name = variant < variants.size() ? variants[variant].c_str() : "Default";
            if (ImGui::BeginCombo("Material variant", name))
            {
                if (ImGui::Selectable("Default", variant == defaultMaterialVariant))
                    object.materialVariant = defaultMaterialVariant;
                for (uint32_t i = 0; i < variants.size(); ++i)
                {
                    ImGui::PushID(i);
                    if (ImGui::Selectable(variants[i].c_str(), variant == i))
                        object.materialVariant = i;
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
        }
        if (!asset.model.scene->clips.empty())
            drawAnimation(object, *asset.model.scene);
    }
    if (ImGui::CollapsingHeader("Rendering"))
        drawRenderingSettings();
}

void Editor::drawAnimation(Object& object, const ModelScene& scene)
{
    ImGui::SeparatorText("Animation");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##AnimationClip", scene.clips[object.clip].name.c_str()))
    {
        for (size_t i = 0; i < scene.clips.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(scene.clips[i].name.c_str(), object.clip == i))
            {
                object.clip = i;
                object.animationTime = 0.0f;
                object.playing = false;
                evaluatePose(scene, scene.clips[i], object.animationTime, object.pose);
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    const auto& clip = scene.clips[object.clip];
    if (ImGui::Button(object.playing ? "Pause" : "Play"))
    {
        object.playing = !object.playing && clip.duration > 0.0f;
        if (object.playing && object.animationTime >= clip.duration)
            object.animationTime = 0.0f;
        evaluatePose(scene, clip, object.animationTime, object.pose);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset"))
    {
        object.playing = false;
        object.animationTime = 0.0f;
        object.pose.world.clear();
    }
    if (clip.duration <= 0.0f)
        return;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##AnimationTime", &object.animationTime, 0.0f, clip.duration, "%.2f s",
                           ImGuiSliderFlags_AlwaysClamp))
    {
        object.playing = false;
        evaluatePose(scene, clip, object.animationTime, object.pose);
    }
}

std::optional<size_t> Editor::pickObject(const EditorViewport& view, glm::vec2 mouse) const
{
    const auto ray = view.ray(mouse);
    float nearest = std::numeric_limits<float>::infinity();
    std::optional<size_t> picked;
    for (size_t i = 0; i < objects_.size(); ++i)
    {
        const auto& object = objects_[i];
        const auto& bounds =
            object.pose.world.empty() ? assets_[object.asset].model.bounds : object.pose.bounds;
        if (const auto distance = hitBounds(ray, bounds, object.transform());
            distance && *distance < nearest)
        {
            nearest = *distance;
            picked = i;
        }
    }
    return picked;
}

void Editor::frame(float deltaTime)
{
    syncOverlay();
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    const float uiScale = updateStyle();
    ImGui::NewFrame();
    pollImport();
    for (auto& object : objects_)
        if (object.playing)
        {
            const auto& scene = *assets_[object.asset].model.scene;
            const auto& clip = scene.clips[object.clip];
            object.animationTime = std::fmod(object.animationTime + deltaTime, clip.duration);
            evaluatePose(scene, clip, object.animationTime, object.pose);
        }
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
    const auto scale = io.DisplayFramebufferScale;
    const VkRect2D viewport{{int32_t(viewStart.x * scale.x), int32_t(viewStart.y * scale.y)},
                            {uint32_t(std::max(viewSize.x * scale.x, 1.0f)),
                             uint32_t(std::max(viewSize.y * scale.y, 1.0f))}};
    const float aspect = float(viewport.extent.width) / float(viewport.extent.height);
    const float tanHalfFov = glm::tan(glm::radians(VulkanRenderer::verticalFovDegrees * 0.5f));
    const bool hovered =
        ImGui::IsMouseHoveringRect(viewStart, viewEnd, false) && !io.WantCaptureMouse;
    const bool wasNavigating = camera_.navigating();
    camera_.update(window_, deltaTime, 2.0f * tanHalfFov / viewSize.y, io.MouseWheel,
                   (!hovered && !wasNavigating) || gizmo_.dragging(), io.WantCaptureKeyboard);
    EditorViewport view{camera_.view(),
                        {viewStart.x, viewStart.y},
                        {viewSize.x, viewSize.y},
                        aspect,
                        tanHalfFov,
                        camera_.nearPlane(),
                        uiScale};
    const bool gizmoInput = hovered && !wasNavigating && !camera_.navigating() && !io.AppFocusLost;
    const auto previousSelection = selectedObject_;
    const glm::vec2 mouse(io.MousePos.x, io.MousePos.y);
    constexpr auto leftButton = ImGuiMouseButton_Left;
    if (pickPress_)
    {
        const bool click = gizmoInput && !gizmo_.dragging() &&
                           glm::distance(mouse, *pickPress_) <= io.MouseDragThreshold;
        if (click && ImGui::IsMouseReleased(leftButton))
            selectedObject_ = pickObject(view, mouse);
        if (!click || !ImGui::IsMouseDown(leftButton))
            pickPress_.reset();
    }

    const bool sceneWasEmpty = objects_.empty();
    if (beginPanel("Scene", {gap, gap}, {left, topHeight}))
        drawScene();
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
    if (sceneWasEmpty && !objects_.empty())
    {
        const auto& model = assets_[objects_.front().asset].model;
        camera_.frameModel(model.center, model.radius, aspect);
        view.view = camera_.view();
        view.nearPlane = camera_.nearPlane();
    }
    if (selectedObject_ != previousSelection)
        gizmo_.endDrag();
    gizmo_.updateTool(gizmoInput);
    if (selectedObject_)
    {
        auto& object = objects_[*selectedObject_];
        gizmo_.edit(view, gizmoInput, object.position, object.rotation, object.scale);
    }
    if (gizmoInput && !gizmo_.dragging() && ImGui::IsMouseClicked(leftButton))
        pickPress_ = mouse;
    if (beginPanel("Details", {width - right - gap, gap}, {right, topHeight}))
        drawDetails();
    ImGui::End();
    drawImportNotification(width - gap * 2.0f, height - gap * 2.0f, uiScale);
    ImGui::Render();
    std::vector<ModelInstance> scene;
    scene.reserve(objects_.size());
    for (size_t i = 0; i < objects_.size(); ++i)
    {
        const auto& object = objects_[i];
        scene.push_back({assets_[object.asset].model, object.transform(), selectedObject_ == i,
                         object.pose.world.empty() ? nullptr : &object.pose,
                         object.materialVariant});
    }
    renderer_.draw(scene, view.view, camera_.position(), camera_.nearPlane(), viewport,
                   [&](VkCommandBuffer command)
                   {
                       const auto target = renderer_.overlayTarget();
                       if (target.format == overlay_.format &&
                           target.imageCount == overlay_.imageCount)
                       {
                           auto queueLock = renderer_.lockQueues();
                           ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command);
                       }
                   });
}

void Editor::drawRenderingSettings()
{
    auto& settings = renderer_.settings;
    const float fps = ImGui::GetIO().Framerate;
    ImGui::Text("%.1f FPS / %.2f ms", fps, 1000.0f / std::max(fps, 1.0f));
    float exposureStops = std::log2(settings.exposure);
    if (ImGui::SliderFloat("Exposure (stops)", &exposureStops, -14.0f, 3.0f, "%+.1f",
                           ImGuiSliderFlags_AlwaysClamp))
        settings.exposure = std::exp2(exposureStops);
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
