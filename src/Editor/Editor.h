#pragma once

#include "Camera.h"
#include "VulkanRenderer.h"

#include <filesystem>
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
    void addObject(size_t asset);
    void drawScene();
    void drawAssets();
    void drawDetails();
    void drawRenderingSettings();

    GLFWwindow* window_;
    VulkanRenderer& renderer_;
    Camera camera_;
    OverlayTarget overlay_{};
    std::filesystem::path importDirectory_;
    std::vector<Asset> assets_;
    std::vector<Object> objects_;
    std::optional<size_t> selectedAsset_;
    std::optional<size_t> selectedObject_;
    float appliedScale_ = 0.0f;
    int selectedLight_ = 0;
    char filter_[64]{};
    std::string error_;
};
