#pragma once

#include "ImportFiles.h"

ImportedModel importFbx(const std::filesystem::path& source, const std::filesystem::path& directory,
                        const ImportProgress& progress = {});
