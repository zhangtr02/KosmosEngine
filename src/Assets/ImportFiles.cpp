#include "ImportFiles.h"

#include <cctype>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <utility>

namespace fs = std::filesystem;

fs::path utf8Path(std::string_view text)
{
    return fs::path(std::u8string(text.begin(), text.end()));
}

std::string fileExtension(const fs::path& path)
{
    auto extension = pathUtf8(path.extension());
    std::ranges::transform(extension, extension.begin(),
                           [](unsigned char c)
                           {
                               return static_cast<char>(std::tolower(c));
                           });
    return extension;
}

void removeStagedModel(const fs::path& file) noexcept
{
    if (file.empty())
        return;
    std::error_code ignored;
    fs::remove_all(file.parent_path(), ignored);
}

StagedModel::StagedModel(const fs::path& source, const fs::path& directory,
                         std::span<const fs::path> dependencies, const ImportProgress& progress)
{
    const auto sourceDirectory = fs::canonical(fs::absolute(source).parent_path());
    fs::create_directories(directory);
    auto stem = source.stem();
    if (pathUtf8(stem).starts_with('.'))
        stem = "Model";
    fs::path pending;
    for (int suffix = 1;; ++suffix)
    {
        pending = directory / (".pending-" + std::to_string(suffix));
        if (fs::create_directory(pending))
            break;
    }
    try
    {
        for (const auto& dependency : dependencies)
        {
            if (progress)
                progress(ImportStage::Copying);
            const auto text = pathUtf8(dependency);
            const auto relative = dependency.lexically_normal();
            const auto invalidPath = [&]
            {
                throw std::runtime_error("Dependency must be inside the model folder: " + text);
            };
            if (relative.empty() || relative == "." || relative.has_root_path() ||
                *relative.begin() == ".." || text.find('\0') != std::string::npos ||
                text.find(':') != std::string::npos)
                invalidPath();
            const auto target = pending / relative;
            if (fs::exists(target))
                continue;
            const auto original = sourceDirectory / relative;
            if (!fs::is_regular_file(original))
                throw std::runtime_error("Missing model dependency: " + text);
            const auto resolved = fs::canonical(original).lexically_relative(sourceDirectory);
            if (resolved.empty() || *resolved.begin() == "..")
                invalidPath();
            fs::create_directories(target.parent_path());
            fs::copy_file(original, target);
        }
        if (progress)
            progress(ImportStage::Copying);
        fs::copy_file(source, pending / source.filename());
        auto name = stem;
        for (int suffix = 1;; ++suffix)
        {
            file_ = directory / name / source.filename();
            if (!fs::exists(file_.parent_path()))
            {
                std::error_code error;
                for (int attempt = 0;; ++attempt)
                {
                    fs::rename(pending, file_.parent_path(), error);
                    if (!error)
                        return;
                    if (fs::exists(file_.parent_path()))
                        break;
                    // A file scanner can hold a new import open briefly on Windows.
                    if (attempt == 4 || (error != std::errc::permission_denied &&
                                         error != std::errc::device_or_resource_busy))
                        throw fs::filesystem_error("Finalize copied model", pending,
                                                   file_.parent_path(), error);
                    std::this_thread::sleep_for(std::chrono::milliseconds(25 * (attempt + 1)));
                    if (progress)
                        progress(ImportStage::Copying);
                }
            }
            name = stem;
            name += "-" + std::to_string(suffix + 1);
        }
    }
    catch (...)
    {
        std::error_code ignored;
        fs::remove_all(pending, ignored);
        throw;
    }
}

StagedModel::~StagedModel() { removeStagedModel(file_); }

ImportedModel StagedModel::finish(Model model)
{
    return {std::exchange(file_, {}), std::move(model)};
}
