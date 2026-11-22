#pragma once

#include <filesystem>
#include <string>
#include <vector>

class Camera;
class VulkanRenderer;
struct GLFWwindow;

class ModelEditor
{
  public:
    explicit ModelEditor(std::filesystem::path directory);
    void draw(GLFWwindow* window, VulkanRenderer& renderer, Camera& camera);

  private:
    std::filesystem::path directory_;
    std::filesystem::path selected_;
    std::vector<std::filesystem::path> models_;
    std::string error_;
};
