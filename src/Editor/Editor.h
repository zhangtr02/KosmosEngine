#pragma once

#include "Camera.h"
#include "TransformGizmo.h"
#include "VulkanRenderer.h"

#include <atomic>
#include <filesystem>
#include <future>
#include <optional>
#include <string>
#include <vector>

struct GLFWwindow;

class Editor
{
  public:
    Editor(GLFWwindow* window, VulkanRenderer& renderer);
    ~Editor();
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;
    void frame(float deltaTime);

  private:
    struct Asset
    {
        std::string name;
        ModelHandle model;
    };

    struct Object
    {
        size_t asset;
        glm::vec3 position{0.0f};
        glm::vec3 rotation{0.0f};
        glm::vec3 scale{1.0f};
        glm::mat4 transform() const;
    };

    void syncOverlay();
    void destroyOverlay();
    float updateStyle();
    void importModel();
    void pollImport();
    void showImportResult(const char* result);
    void drawImportNotification(float right, float bottom, float scale);
    void addObject(size_t asset);
    void drawScene();
    void drawAssets();
    void drawDetails();
    void drawRenderingSettings();
    std::optional<size_t> pickObject(const EditorViewport& view, glm::vec2 mouse) const;

    GLFWwindow* window_;
    VulkanRenderer& renderer_;
    Camera camera_;
    TransformGizmo gizmo_;
    OverlayTarget overlay_{};
    std::filesystem::path importDirectory_;
    std::atomic<ImportStage> importStage_{ImportStage::Copying};
    std::atomic<bool> stopImport_{false};
    std::future<Asset> importJob_;
    std::string importName_;
    const char* importResult_ = nullptr;
    double importNoticeUntil_ = 0.0;
    std::vector<Asset> assets_;
    std::vector<Object> objects_;
    std::optional<size_t> selectedAsset_;
    std::optional<size_t> selectedObject_;
    std::optional<glm::vec2> pickPress_;
    float appliedScale_ = 0.0f;
    int selectedLight_ = 0;
    char filter_[64]{};
    std::string error_;
};
