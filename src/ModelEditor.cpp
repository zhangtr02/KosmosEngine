#include "ModelEditor.h"
#include "Camera.h"
#include "Model.h"
#include "VulkanRenderer.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <imgui.h>
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
} // namespace

ModelEditor::ModelEditor(fs::path directory) : directory_(std::move(directory))
{
    try
    {
        if (!fs::exists(directory_))
            return;
        for (const auto& folder : fs::directory_iterator(directory_))
        {
            if (!folder.is_directory() || pathUtf8(folder.path().filename()).starts_with('.'))
                continue;
            for (const auto& entry : fs::directory_iterator(folder.path()))
                if (entry.is_regular_file() && entry.path().extension() == ".gltf")
                    models_.push_back(entry.path());
        }
        std::ranges::sort(models_);
    }
    catch (const std::exception& error)
    {
        error_ = error.what();
    }
}

void ModelEditor::draw(GLFWwindow* window, VulkanRenderer& renderer, Camera& camera)
{
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 12, 12), ImGuiCond_Always,
                            ImVec2(1, 0));
    ImGui::SetNextWindowSize(ImVec2(260, 300), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Assets"))
    {
        const bool import = ImGui::Button("Import Model...");
        ImGui::Separator();
        fs::path requested;
        for (const auto& model : models_)
        {
            const auto id = pathUtf8(model);
            const auto name = pathUtf8(model.parent_path().filename());
            ImGui::PushID(id.c_str());
            if (ImGui::Selectable(name.c_str(), selected_ == model))
                requested = model;
            ImGui::PopID();
        }
        if (models_.empty())
            ImGui::TextDisabled("No imported models");
        if (import || !requested.empty())
        {
            fs::path imported;
            try
            {
                if (import)
                {
                    const auto source = chooseModel(window);
                    if (!source.empty())
                    {
                        models_.reserve(models_.size() + 1);
                        imported = copyModel(source, directory_);
                        requested = imported;
                    }
                }
                if (!requested.empty())
                {
                    const float radius = renderer.setModel(requested);
                    int width, height;
                    glfwGetFramebufferSize(window, &width, &height);
                    camera.frameModel(radius,
                                      float(std::max(width, 1)) / float(std::max(height, 1)));
                    selected_.swap(requested);
                    if (!imported.empty())
                        models_.push_back(std::move(imported));
                    error_.clear();
                }
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
        if (!error_.empty())
            ImGui::TextWrapped("%s", error_.c_str());
    }
    ImGui::End();
}
