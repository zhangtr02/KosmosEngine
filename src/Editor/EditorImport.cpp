#include "Editor.h"
#include "FbxImporter.h"
#include "GltfImporter.h"

#include <GLFW/glfw3.h>
#include <chrono>
#include <imgui.h>
#include <iterator>
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
    dialog.lpstrFilter = L"Models (*.gltf, *.glb, *.fbx)\0*.gltf;*.glb;*.fbx\0";
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

} // namespace

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
        importJob_ = std::async(
            std::launch::async,
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
                    const auto extension = fileExtension(source);
                    if (extension != ".gltf" && extension != ".glb" && extension != ".fbx")
                        throw std::runtime_error("Choose a .gltf, .glb, or .fbx model.");
                    auto asset = extension == ".fbx" ? importFbx(source, directory, report)
                                                     : importGltf(source, directory, report);
                    imported = std::move(asset.file);
                    auto name = pathUtf8(imported.parent_path().filename());
                    auto& model = asset.model;
                    auto xmp = std::move(model.xmp);
                    return Asset{std::move(name), renderer_.uploadModel(std::move(model), report),
                                 std::move(xmp)};
                }
                catch (...)
                {
                    removeStagedModel(imported);
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
