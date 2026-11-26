#pragma once

#include "Model.h"

#include <span>
#include <string_view>

struct ImportedModel
{
    std::filesystem::path file;
    Model model;
};

std::filesystem::path utf8Path(std::string_view text);
std::string fileExtension(const std::filesystem::path& path);
void removeStagedModel(const std::filesystem::path& file) noexcept;

class StagedModel
{
  public:
    StagedModel(const std::filesystem::path& source, const std::filesystem::path& directory,
                std::span<const std::filesystem::path> dependencies,
                const ImportProgress& progress);
    ~StagedModel();
    StagedModel(const StagedModel&) = delete;
    StagedModel& operator=(const StagedModel&) = delete;

    const std::filesystem::path& file() const { return file_; }
    ImportedModel finish(Model model);

  private:
    std::filesystem::path file_;
};
