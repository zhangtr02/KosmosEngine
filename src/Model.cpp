#include "Model.h"

#include <algorithm>
#include <cctype>
#include <cgltf.h>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
std::filesystem::path utf8Path(std::string_view text)
{
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

cgltf_options fileOptions()
{
    cgltf_options options{};
    options.file.read = [](const cgltf_memory_options*, const cgltf_file_options*, const char* path,
                           cgltf_size* size, void** data) -> cgltf_result
    {
        std::ifstream file(utf8Path(path), std::ios::binary | std::ios::ate);
        if (!file)
            return cgltf_result_file_not_found;
        const auto length = file.tellg();
        if (length <= 0 || (*size && *size > static_cast<size_t>(length)))
            return cgltf_result_io_error;
        if (!*size)
            *size = static_cast<size_t>(length);
        std::unique_ptr<void, decltype(&std::free)> bytes(std::malloc(*size), std::free);
        if (!bytes)
            return cgltf_result_out_of_memory;
        file.seekg(0);
        if (!file.read(static_cast<char*>(bytes.get()), static_cast<std::streamsize>(*size)))
            return cgltf_result_io_error;
        *data = bytes.release();
        return cgltf_result_success;
    };
    return options;
}

auto parseGltf(const cgltf_options& options, const std::string& filename)
{
    cgltf_data* raw = nullptr;
    const auto result = cgltf_parse_file(&options, filename.c_str(), &raw);
    std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(raw, cgltf_free);
    if (result != cgltf_result_success)
        throw std::runtime_error("Cannot parse glTF: " + filename);
    return data;
}
} // namespace

std::filesystem::path copyModel(const std::filesystem::path& source,
                                const std::filesystem::path& directory)
{
    namespace fs = std::filesystem;
    auto extension = pathUtf8(source.extension());
    std::ranges::transform(extension, extension.begin(),
                           [](unsigned char c)
                           {
                               return static_cast<char>(std::tolower(c));
                           });
    if (extension != ".gltf")
        throw std::runtime_error("Choose a .gltf model with external files.");
    const auto data = parseGltf(fileOptions(), pathUtf8(source));
    if (data->file_type != cgltf_file_type_gltf)
        throw std::runtime_error("Expected a JSON .gltf model.");
    const auto sourceDirectory = fs::canonical(fs::absolute(source).parent_path());
    fs::create_directories(directory);
    auto stem = source.stem();
    if (pathUtf8(stem).starts_with('.'))
        stem = "Model";
    auto destination = directory / stem;
    for (int suffix = 2; fs::exists(destination); ++suffix)
    {
        auto name = stem;
        name += "-" + std::to_string(suffix);
        destination = directory / name;
    }
    auto filename = source.filename();
    filename.replace_extension(".gltf");
    const auto imported = destination / filename;
    fs::path pending;
    for (int suffix = 1;; ++suffix)
    {
        pending = directory / (".pending-" + std::to_string(suffix));
        if (fs::create_directory(pending))
            break;
    }
    try
    {
        const auto copyDependency = [&](const char* uri)
        {
            if (!uri || std::string_view(uri).starts_with("data:"))
                return;
            std::string decoded(uri);
            decoded.resize(cgltf_decode_uri(decoded.data()));
            const auto invalidPath = [&]
            {
                throw std::runtime_error(
                    std::string("Dependency must be inside the model folder: ") + uri);
            };
            if (decoded.find('\0') != std::string::npos || decoded.find(':') != std::string::npos)
                invalidPath();
            const auto relative = utf8Path(decoded).lexically_normal();
            if (relative.empty() || relative.has_root_path() || *relative.begin() == "..")
                invalidPath();
            const auto original = sourceDirectory / relative;
            if (!fs::is_regular_file(original))
                throw std::runtime_error(std::string("Missing model dependency: ") + uri);
            const auto resolved = fs::canonical(original).lexically_relative(sourceDirectory);
            if (resolved.empty() || *resolved.begin() == "..")
                invalidPath();
            const auto target = pending / relative;
            fs::create_directories(target.parent_path());
            if (!fs::exists(target))
                fs::copy_file(original, target);
        };
        for (size_t i = 0; i < data->buffers_count; ++i)
            copyDependency(data->buffers[i].uri);
        for (size_t i = 0; i < data->images_count; ++i)
            copyDependency(data->images[i].uri);
        fs::copy_file(source, pending / imported.filename());
        fs::rename(pending, destination);
    }
    catch (...)
    {
        std::error_code ignored;
        fs::remove_all(pending, ignored);
        throw;
    }
    return imported;
}

Model loadModel(const std::filesystem::path& path)
{
    const auto filename = pathUtf8(path);
    const auto require = [&](bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error("Cannot load model: " + filename + " (" + message + ")");
    };
    const auto options = fileOptions();
    const auto data = parseGltf(options, filename);
    require(cgltf_load_buffers(&options, data.get(), filename.c_str()) == cgltf_result_success,
            "buffer load failed");
    require(cgltf_validate(data.get()) == cgltf_result_success, "invalid glTF data");
    require(data->animations_count == 0, "animations are unsupported");
    for (cgltf_size i = 0; i < data->extensions_required_count; ++i)
        if (std::string_view(data->extensions_required[i]) != "KHR_materials_emissive_strength")
            throw std::runtime_error(std::string("Unsupported required glTF extension: ") +
                                     data->extensions_required[i]);

    Model model;
    const auto getTexture = [&](const cgltf_texture_view& view, bool srgb, bool flatNormal = false)
    {
        TextureSource source{{}, srgb, flatNormal};
        if (view.texture)
        {
            const auto* image = view.texture->image;
            require(image && image->uri && image->uri[0] && !image->buffer_view &&
                        !std::string(image->uri).starts_with("data:") && view.texcoord == 0 &&
                        !view.has_transform,
                    "an external texture using UV0 is required");
            std::string uri = image->uri;
            uri.resize(cgltf_decode_uri(uri.data()));
            source.path = (path.parent_path() / utf8Path(uri)).lexically_normal();
        }
        const auto found = std::find(model.textures.begin(), model.textures.end(), source);
        const auto index = static_cast<uint32_t>(found - model.textures.begin());
        if (found == model.textures.end())
            model.textures.push_back(source);
        return index;
    };
    std::vector<const cgltf_material*> materialSources;
    const auto getMaterial = [&](const cgltf_material* source)
    {
        const auto found = std::find(materialSources.begin(), materialSources.end(), source);
        if (found != materialSources.end())
            return static_cast<uint32_t>(found - materialSources.begin());
        Material material;
        require(!source || source->alpha_mode == cgltf_alpha_mode_opaque ||
                    source->alpha_mode == cgltf_alpha_mode_mask ||
                    source->alpha_mode == cgltf_alpha_mode_blend,
                "invalid alpha mode");
        if (source)
        {
            material.alphaMode = source->alpha_mode == cgltf_alpha_mode_mask    ? AlphaMode::Mask
                                 : source->alpha_mode == cgltf_alpha_mode_blend ? AlphaMode::Blend
                                                                                : AlphaMode::Opaque;
            material.alphaCutoff = source->alpha_cutoff;
            material.doubleSided = source->double_sided;
        }
        require(material.alphaMode != AlphaMode::Mask ||
                    (std::isfinite(material.alphaCutoff) && material.alphaCutoff >= 0.0f),
                "invalid alpha cutoff");
        cgltf_texture_view baseTexture{};
        cgltf_texture_view metallicRoughnessTexture{};
        if (source && source->has_pbr_metallic_roughness)
        {
            material.baseColorFactor =
                glm::make_vec4(source->pbr_metallic_roughness.base_color_factor);
            baseTexture = source->pbr_metallic_roughness.base_color_texture;
            metallicRoughnessTexture = source->pbr_metallic_roughness.metallic_roughness_texture;
            material.metallicFactor = source->pbr_metallic_roughness.metallic_factor;
            material.roughnessFactor = source->pbr_metallic_roughness.roughness_factor;
        }
        material.textureIndex = getTexture(baseTexture, true);
        material.normalTextureIndex =
            getTexture(source ? source->normal_texture : cgltf_texture_view{}, false, true);
        material.metallicRoughnessTextureIndex = getTexture(metallicRoughnessTexture, false);
        material.occlusionTextureIndex =
            getTexture(source ? source->occlusion_texture : cgltf_texture_view{}, false);
        material.emissiveTextureIndex =
            getTexture(source ? source->emissive_texture : cgltf_texture_view{}, true);
        if (source)
        {
            material.emissiveFactor = glm::make_vec3(source->emissive_factor);
            if (source->has_emissive_strength)
                material.emissiveStrength = source->emissive_strength.emissive_strength;
        }
        require(std::isfinite(material.emissiveStrength) && material.emissiveStrength >= 0.0f,
                "invalid emissive strength");
        for (int component = 0; component < 3; ++component)
            require(std::isfinite(material.emissiveFactor[component]) &&
                        material.emissiveFactor[component] >= 0.0f &&
                        material.emissiveFactor[component] <= 1.0f,
                    "invalid emissive factor");
        if (source && source->normal_texture.texture)
            material.normalScale = source->normal_texture.scale;
        if (source && source->occlusion_texture.texture)
            material.occlusionStrength = source->occlusion_texture.scale;
        require(std::isfinite(material.normalScale), "invalid normal scale");
        require(std::isfinite(material.occlusionStrength) && material.occlusionStrength >= 0.0f &&
                    material.occlusionStrength <= 1.0f,
                "invalid occlusion strength");
        require(std::isfinite(material.metallicFactor) && material.metallicFactor >= 0.0f &&
                    material.metallicFactor <= 1.0f && std::isfinite(material.roughnessFactor) &&
                    material.roughnessFactor >= 0.0f && material.roughnessFactor <= 1.0f,
                "invalid metallic or roughness factor");
        for (int component = 0; component < 4; ++component)
            require(std::isfinite(material.baseColorFactor[component]) &&
                        material.baseColorFactor[component] >= 0.0f &&
                        material.baseColorFactor[component] <= 1.0f,
                    "invalid base-color factor");
        materialSources.push_back(source);
        model.materials.push_back(material);
        return static_cast<uint32_t>(model.materials.size() - 1);
    };

    const auto* scene =
        data->scene ? data->scene : (data->scenes_count ? &data->scenes[0] : nullptr);
    require(scene != nullptr, "no scene");
    std::vector<const cgltf_node*> nodes;
    for (cgltf_size i = 0; i < scene->nodes_count; ++i)
        nodes.push_back(scene->nodes[i]);
    glm::vec3 minimum(std::numeric_limits<float>::max());
    glm::vec3 maximum(std::numeric_limits<float>::lowest());
    while (!nodes.empty())
    {
        const auto* node = nodes.back();
        nodes.pop_back();
        for (cgltf_size i = 0; i < node->children_count; ++i)
            nodes.push_back(node->children[i]);
        if (!node->mesh)
            continue;
        require(!node->skin && !node->has_mesh_gpu_instancing, "only static meshes are supported");
        glm::mat4 transform;
        cgltf_node_transform_world(node, glm::value_ptr(transform));
        const float determinant = glm::determinant(glm::mat3(transform));
        require(std::isfinite(determinant) && determinant != 0.0f, "singular node transform");
        const auto normalTransform = glm::transpose(glm::inverse(glm::mat3(transform)));
        for (cgltf_size p = 0; p < node->mesh->primitives_count; ++p)
        {
            const auto& primitive = node->mesh->primitives[p];
            require(primitive.type == cgltf_primitive_type_triangles && primitive.indices &&
                        primitive.targets_count == 0 && !primitive.has_draco_mesh_compression,
                    "an uncompressed indexed triangle mesh is required");
            const auto materialIndex = getMaterial(primitive.material);
            const auto* positions =
                cgltf_find_accessor(&primitive, cgltf_attribute_type_position, 0);
            const auto* normals = cgltf_find_accessor(&primitive, cgltf_attribute_type_normal, 0);
            const auto* uvs = cgltf_find_accessor(&primitive, cgltf_attribute_type_texcoord, 0);
            const auto* tangents = cgltf_find_accessor(&primitive, cgltf_attribute_type_tangent, 0);
            const auto& material = model.materials[materialIndex];
            const bool normalMapped = !model.textures[material.normalTextureIndex].path.empty();
            const bool textured =
                !model.textures[material.textureIndex].path.empty() || normalMapped ||
                !model.textures[material.metallicRoughnessTextureIndex].path.empty() ||
                !model.textures[material.occlusionTextureIndex].path.empty() ||
                !model.textures[material.emissiveTextureIndex].path.empty();
            require(positions && normals && (uvs || !textured),
                    "POSITION, NORMAL and textured UV0 are required");
            require(!normalMapped || tangents, "normal maps require TANGENT");
            require(positions->type == cgltf_type_vec3 && normals->type == cgltf_type_vec3 &&
                        (!uvs || uvs->type == cgltf_type_vec2) &&
                        (!tangents || tangents->type == cgltf_type_vec4),
                    "invalid vertex attribute types");
            require(positions->count > 0 && positions->count <= INT32_MAX - model.vertices.size() &&
                        primitive.indices->count > 0 &&
                        primitive.indices->count <= UINT32_MAX - model.indices.size() &&
                        primitive.indices->count % 3 == 0,
                    "invalid mesh size");
            Draw draw{static_cast<uint32_t>(model.indices.size()),
                      static_cast<uint32_t>(primitive.indices->count),
                      static_cast<int32_t>(model.vertices.size()), materialIndex};
            model.vertices.resize(model.vertices.size() + positions->count);
            model.indices.resize(model.indices.size() + primitive.indices->count);
            require(cgltf_accessor_unpack_indices(
                        primitive.indices, model.indices.data() + draw.firstIndex, sizeof(uint32_t),
                        draw.indexCount) == draw.indexCount,
                    "index read failed");
            for (uint32_t i = 0; i < draw.indexCount; ++i)
                require(model.indices[draw.firstIndex + i] < positions->count,
                        "index exceeds vertex count");
            // Node transforms are baked into vertices; preserve CCW fronts after mirroring.
            if (determinant < 0.0f)
                for (uint32_t i = 0; i < draw.indexCount; i += 3)
                    std::swap(model.indices[draw.firstIndex + i + 1],
                              model.indices[draw.firstIndex + i + 2]);
            for (cgltf_size i = 0; i < positions->count; ++i)
            {
                auto& vertex = model.vertices[draw.vertexOffset + i];
                vertex.uv = glm::vec2(0.0f);
                require(
                    cgltf_accessor_read_float(positions, i, glm::value_ptr(vertex.position), 3) &&
                        cgltf_accessor_read_float(normals, i, glm::value_ptr(vertex.normal), 3) &&
                        (!uvs || cgltf_accessor_read_float(uvs, i, glm::value_ptr(vertex.uv), 2)) &&
                        (!tangents ||
                         cgltf_accessor_read_float(tangents, i, glm::value_ptr(vertex.tangent), 4)),
                    "vertex read failed (sparse accessors are unsupported)");
                vertex.position = glm::vec3(transform * glm::vec4(vertex.position, 1.0f));
                vertex.normal = normalTransform * vertex.normal;
                const float length = glm::length(vertex.normal);
                require(std::isfinite(length) && length > 0.0f, "invalid vertex normal");
                vertex.normal /= length;
                if (tangents)
                {
                    auto tangent = glm::mat3(transform) * glm::vec3(vertex.tangent);
                    tangent -= vertex.normal * glm::dot(vertex.normal, tangent);
                    const float tangentLength = glm::length(tangent);
                    require(std::isfinite(tangentLength) && tangentLength > 0.0f &&
                                (vertex.tangent.w == 1.0f || vertex.tangent.w == -1.0f),
                            "invalid vertex tangent");
                    vertex.tangent =
                        glm::vec4(tangent / tangentLength,
                                  vertex.tangent.w * (determinant < 0.0f ? -1.0f : 1.0f));
                }
                for (int axis = 0; axis < 3; ++axis)
                    require(std::isfinite(vertex.position[axis]), "invalid vertex position");
                require(std::isfinite(vertex.uv.x) && std::isfinite(vertex.uv.y), "invalid UV");
                minimum = glm::min(minimum, vertex.position);
                maximum = glm::max(maximum, vertex.position);
            }
            glm::vec3 partMinimum(std::numeric_limits<float>::max());
            glm::vec3 partMaximum(std::numeric_limits<float>::lowest());
            for (uint32_t i = 0; i < draw.indexCount; ++i)
            {
                const auto& position =
                    model.vertices[draw.vertexOffset + model.indices[draw.firstIndex + i]].position;
                partMinimum = glm::min(partMinimum, position);
                partMaximum = glm::max(partMaximum, position);
            }
            draw.center = (partMinimum + partMaximum) * 0.5f;
            model.draws.push_back(draw);
        }
    }
    require(!model.draws.empty(), "no drawable meshes");
    const auto size = maximum - minimum;
    const float longest = std::max({size.x, size.y, size.z});
    require(std::isfinite(longest) && longest > 0.0f, "empty model bounds");
    const auto center = (minimum + maximum) * 0.5f;
    for (auto& vertex : model.vertices)
        vertex.position = (vertex.position - center) * (2.0f / longest);
    for (auto& draw : model.draws)
        draw.center = (draw.center - center) * (2.0f / longest);
    return model;
}
