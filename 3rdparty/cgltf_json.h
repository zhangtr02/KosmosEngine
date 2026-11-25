#pragma once

#include <optional>
#include <string_view>
#include <vector>

struct cgltf_data;
struct cgltf_texture_view;

struct GltfJsonValue
{
    std::string_view text;
    bool isString;
};

std::optional<GltfJsonValue> gltfJsonProperty(std::string_view json, std::string_view key);
std::vector<GltfJsonValue> gltfJsonArray(std::string_view json);
bool gltfTextureView(std::string_view json, const cgltf_data& data, cgltf_texture_view& view);
