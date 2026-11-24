#pragma once

#include <optional>
#include <string_view>

struct GltfJsonValue
{
    std::string_view text;
    bool isString;
};

std::optional<GltfJsonValue> gltfJsonProperty(std::string_view json, std::string_view key);
