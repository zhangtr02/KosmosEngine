#include "Model.h"

#include <algorithm>
#include <cgltf.h>
#include <cmath>
#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

Model loadModel(const std::filesystem::path& path)
{
    const auto require = [&](bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error("Cannot load model: " + path.string() + " (" + message + ")");
    };
    cgltf_options options{};
    cgltf_data* raw = nullptr;
    const auto result = cgltf_parse_file(&options, path.string().c_str(), &raw);
    std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(raw, cgltf_free);
    require(result == cgltf_result_success, "glTF parse failed");
    require(cgltf_load_buffers(&options, data.get(), path.string().c_str()) == cgltf_result_success,
            "buffer load failed");
    require(cgltf_validate(data.get()) == cgltf_result_success, "invalid glTF data");
    require(data->animations_count == 0, "animations are unsupported");
    for (cgltf_size i = 0; i < data->extensions_required_count; ++i)
        require(std::string(data->extensions_required[i]) == "KHR_materials_emissive_strength",
                "unsupported required extension");

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
            source.path = (path.parent_path() / uri).lexically_normal();
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
                    source->alpha_mode == cgltf_alpha_mode_mask,
                "only OPAQUE and MASK materials are supported");
        if (source)
        {
            material.alphaMask = source->alpha_mode == cgltf_alpha_mode_mask;
            material.alphaCutoff = source->alpha_cutoff;
            material.doubleSided = source->double_sided;
        }
        require(!material.alphaMask ||
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
            const Draw draw{static_cast<uint32_t>(model.indices.size()),
                            static_cast<uint32_t>(primitive.indices->count),
                            static_cast<int32_t>(model.vertices.size()), materialIndex};
            model.vertices.resize(model.vertices.size() + positions->count);
            model.indices.resize(model.indices.size() + primitive.indices->count);
            require(cgltf_accessor_unpack_indices(
                        primitive.indices, model.indices.data() + draw.firstIndex, sizeof(uint32_t),
                        draw.indexCount) == draw.indexCount,
                    "index read failed");
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
    return model;
}
