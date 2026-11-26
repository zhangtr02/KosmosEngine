#pragma once

#include "ImportFiles.h"

ImportedModel importGltf(const std::filesystem::path& source,
                         const std::filesystem::path& directory,
                         const ImportProgress& progress = {});
