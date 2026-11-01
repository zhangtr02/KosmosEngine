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
    require(data->extensions_required_count == 0 && data->animations_count == 0,
            "required extensions and animations are unsupported");

    const auto* scene =
        data->scene ? data->scene : (data->scenes_count ? &data->scenes[0] : nullptr);
    require(scene != nullptr, "no scene");
    const cgltf_node* meshNode = nullptr;
    std::vector<const cgltf_node*> nodes;
    for (cgltf_size i = 0; i < scene->nodes_count; ++i)
        nodes.push_back(scene->nodes[i]);
    while (!nodes.empty())
    {
        const auto* node = nodes.back();
        nodes.pop_back();
        if (node->mesh)
        {
            require(!meshNode && !node->skin && !node->has_mesh_gpu_instancing,
                    "only one static mesh node is supported");
            meshNode = node;
        }
        for (cgltf_size i = 0; i < node->children_count; ++i)
            nodes.push_back(node->children[i]);
    }
    require(meshNode && meshNode->mesh->primitives_count == 1, "only one primitive is supported");
    const auto& primitive = meshNode->mesh->primitives[0];
    require(primitive.type == cgltf_primitive_type_triangles && primitive.indices &&
                primitive.targets_count == 0 && !primitive.has_draco_mesh_compression,
            "an uncompressed indexed triangle mesh is required");
    const auto* positions = cgltf_find_accessor(&primitive, cgltf_attribute_type_position, 0);
    const auto* normals = cgltf_find_accessor(&primitive, cgltf_attribute_type_normal, 0);
    const auto* uvs = cgltf_find_accessor(&primitive, cgltf_attribute_type_texcoord, 0);
    require(positions && normals && uvs, "POSITION, NORMAL and TEXCOORD_0 are required");
    require(positions->type == cgltf_type_vec3 && normals->type == cgltf_type_vec3 &&
                uvs->type == cgltf_type_vec2,
            "invalid vertex attribute types");
    require(positions->count > 0 && positions->count <= UINT32_MAX &&
                primitive.indices->count > 0 && primitive.indices->count <= UINT32_MAX &&
                primitive.indices->count % 3 == 0,
            "invalid mesh size");

    const auto* material = primitive.material;
    require(material && material->has_pbr_metallic_roughness &&
                material->alpha_mode == cgltf_alpha_mode_opaque,
            "an opaque base-color material is required");
    const auto& baseColor = material->pbr_metallic_roughness;
    require(glm::make_vec4(baseColor.base_color_factor) == glm::vec4(1.0f),
            "base-color factors are unsupported");
    const auto& texture = baseColor.base_color_texture;
    require(texture.texture && texture.texture->image && texture.texcoord == 0 &&
                !texture.has_transform,
            "a base-color texture using UV0 is required");
    const auto* image = texture.texture->image;
    require(image->uri && !image->buffer_view && !std::string(image->uri).starts_with("data:"),
            "an external texture image is required");
    std::string uri = image->uri;
    uri.resize(cgltf_decode_uri(uri.data()));

    Model model;
    model.texture = path.parent_path() / uri;
    model.vertices.resize(positions->count);
    model.indices.resize(primitive.indices->count);
    require(cgltf_accessor_unpack_indices(primitive.indices, model.indices.data(), sizeof(uint32_t),
                                          model.indices.size()) == model.indices.size(),
            "index read failed");

    glm::mat4 transform;
    cgltf_node_transform_world(meshNode, glm::value_ptr(transform));
    const float determinant = glm::determinant(glm::mat3(transform));
    require(std::isfinite(determinant) && determinant != 0.0f, "singular node transform");
    const auto normalTransform = glm::transpose(glm::inverse(glm::mat3(transform)));
    glm::vec3 minimum(std::numeric_limits<float>::max());
    glm::vec3 maximum(std::numeric_limits<float>::lowest());
    for (cgltf_size i = 0; i < positions->count; ++i)
    {
        auto& vertex = model.vertices[i];
        require(cgltf_accessor_read_float(positions, i, glm::value_ptr(vertex.position), 3) &&
                    cgltf_accessor_read_float(normals, i, glm::value_ptr(vertex.normal), 3) &&
                    cgltf_accessor_read_float(uvs, i, glm::value_ptr(vertex.uv), 2),
                "vertex read failed (sparse accessors are unsupported)");
        vertex.position = glm::vec3(transform * glm::vec4(vertex.position, 1.0f));
        vertex.normal = normalTransform * vertex.normal;
        const float length = glm::length(vertex.normal);
        require(std::isfinite(length) && length > 0.0f, "invalid vertex normal");
        vertex.normal /= length;
        for (int axis = 0; axis < 3; ++axis)
            require(std::isfinite(vertex.position[axis]), "invalid vertex position");
        minimum = glm::min(minimum, vertex.position);
        maximum = glm::max(maximum, vertex.position);
    }
    const auto size = maximum - minimum;
    const float longest = std::max({size.x, size.y, size.z});
    require(std::isfinite(longest) && longest > 0.0f, "empty model bounds");
    const auto center = (minimum + maximum) * 0.5f;
    for (auto& vertex : model.vertices)
        vertex.position = (vertex.position - center) * (2.0f / longest);
    return model;
}
