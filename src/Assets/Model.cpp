#include "Model.h"
#include "../../3rdparty/cgltf_json.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cgltf.h>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <draco/compression/decode.h>
#include <fstream>
#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <memory>
#include <meshoptimizer.h>
#include <mikktspace.h>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>

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

EncodedImage decodeDataImage(const cgltf_options& options, std::string_view uri)
{
    const auto comma = uri.find(',');
    if (comma == std::string_view::npos || !uri.substr(0, comma).ends_with(";base64"))
        return {};
    const auto payload = uri.substr(comma + 1);
    if (payload.size() < 4 || payload.size() % 4 != 0)
        return {};
    const size_t padding = payload.ends_with("==") ? 2 : payload.ends_with('=') ? 1 : 0;
    const size_t size = payload.size() / 4 * 3 - padding;
    void* bytes = nullptr;
    if (size > INT32_MAX ||
        cgltf_load_buffer_base64(&options, size, payload.data(), &bytes) != cgltf_result_success)
        return {};
    return {std::shared_ptr<const uint8_t>(static_cast<uint8_t*>(bytes), std::free), size};
}

bool decodeMeshoptView(cgltf_data& data, cgltf_buffer_view& view)
{
    const auto& compressed = view.meshopt_compression;
    if (!compressed.buffer || !compressed.buffer->data || !compressed.size || !compressed.count ||
        !compressed.stride || view.size % compressed.stride != 0 ||
        view.size / compressed.stride != compressed.count ||
        compressed.offset > compressed.buffer->size ||
        compressed.size > compressed.buffer->size - compressed.offset)
        return false;

    const auto* bytes = static_cast<const uint8_t*>(compressed.buffer->data) + compressed.offset;
    // cgltf owns this override and releases it with the rest of the model buffers.
    auto* decoded = data.memory.alloc_func(data.memory.user_data, view.size);
    if (!decoded)
        throw std::bad_alloc();
    view.data = decoded;
    const auto count = compressed.count, stride = compressed.stride;
    int result;
    switch (compressed.mode)
    {
    case cgltf_meshopt_compression_mode_attributes:
        result = meshopt_decodeVertexBuffer(decoded, count, stride, bytes, compressed.size);
        break;
    case cgltf_meshopt_compression_mode_triangles:
        result = meshopt_decodeIndexBuffer(decoded, count, stride, bytes, compressed.size);
        break;
    case cgltf_meshopt_compression_mode_indices:
        result = meshopt_decodeIndexSequence(decoded, count, stride, bytes, compressed.size);
        break;
    default:
        return false;
    }
    if (result != 0)
        return false;

    switch (compressed.filter)
    {
    case cgltf_meshopt_compression_filter_none:
        break;
    case cgltf_meshopt_compression_filter_octahedral:
        meshopt_decodeFilterOct(decoded, count, stride);
        break;
    case cgltf_meshopt_compression_filter_quaternion:
        meshopt_decodeFilterQuat(decoded, count, stride);
        break;
    case cgltf_meshopt_compression_filter_exponential:
        meshopt_decodeFilterExp(decoded, count, stride);
        break;
    case cgltf_meshopt_compression_filter_color:
        meshopt_decodeFilterColor(decoded, count, stride);
        break;
    default:
        return false;
    }
    return true;
}

template <typename Apply> bool applySparse(const cgltf_accessor& accessor, Apply apply)
{
    if (!accessor.is_sparse)
        return true;

    const auto& sparse = accessor.sparse;
    cgltf_accessor keys{};
    keys.type = cgltf_type_scalar;
    keys.component_type = sparse.indices_component_type;
    keys.count = sparse.count;
    keys.offset = sparse.indices_byte_offset;
    keys.buffer_view = sparse.indices_buffer_view;
    keys.stride = cgltf_component_size(keys.component_type);

    // Sparse replacement values are packed even when the base accessor is interleaved.
    auto values = accessor;
    values.is_sparse = false;
    values.count = sparse.count;
    values.offset = sparse.values_byte_offset;
    values.buffer_view = sparse.values_buffer_view;
    values.stride = cgltf_calc_size(values.type, values.component_type);

    const auto hasData = [](const cgltf_accessor& packed)
    {
        const auto* view = packed.buffer_view;
        return view && packed.stride && packed.offset <= view->size &&
               packed.count <= (view->size - packed.offset) / packed.stride &&
               cgltf_buffer_view_data(view);
    };
    if (!hasData(keys) || !hasData(values))
        return false;

    size_t previous = 0;
    for (size_t i = 0; i < sparse.count; ++i)
    {
        const auto target = cgltf_accessor_read_index(&keys, i);
        if (target >= accessor.count || (i != 0 && target <= previous) || !apply(values, i, target))
            return false;
        previous = target;
    }
    return true;
}

std::pair<PrimitiveTopology, uint64_t> primitiveLayout(cgltf_primitive_type type, uint64_t count)
{
    using enum PrimitiveTopology;
    switch (type)
    {
    case cgltf_primitive_type_points:
        return {Points, count};
    case cgltf_primitive_type_lines:
        return {Lines, count % 2 == 0 ? count : 0};
    case cgltf_primitive_type_line_loop:
        return {Lines, count >= 2 ? 2 * count : 0};
    case cgltf_primitive_type_line_strip:
        return {Lines, count >= 2 ? 2 * (count - 1) : 0};
    case cgltf_primitive_type_triangles:
        return {Triangles, count % 3 == 0 ? count : 0};
    case cgltf_primitive_type_triangle_strip:
    case cgltf_primitive_type_triangle_fan:
        return {Triangles, count >= 3 ? 3 * (count - 2) : 0};
    default:
        return {Triangles, 0};
    }
}

void expandPrimitiveIndices(cgltf_primitive_type type, size_t count, std::span<uint32_t> indices)
{
    // Expand from the end so the original indices can share the output buffer.
    if (type == cgltf_primitive_type_line_loop || type == cgltf_primitive_type_line_strip)
    {
        if (type == cgltf_primitive_type_line_loop)
        {
            indices[2 * (count - 1)] = indices[count - 1];
            indices.back() = indices.front();
        }
        for (size_t i = count - 1; i > 0; --i)
        {
            const auto a = indices[i - 1], b = indices[i];
            indices[2 * (i - 1)] = a;
            indices[2 * (i - 1) + 1] = b;
        }
    }
    else if (type == cgltf_primitive_type_triangle_strip || type == cgltf_primitive_type_triangle_fan)
    {
        const bool fan = type == cgltf_primitive_type_triangle_fan;
        for (size_t i = count - 2; i > 0; --i)
        {
            auto a = indices[fan ? 0 : i - 1], b = indices[i];
            const auto c = indices[i + 1];
            if (!fan && i % 2 == 0)
                std::swap(a, b);
            indices[3 * (i - 1)] = a;
            indices[3 * (i - 1) + 1] = b;
            indices[3 * (i - 1) + 2] = c;
        }
    }
}

template <typename ComponentPointer>
bool readAccessor(const cgltf_accessor& accessor, size_t count, ComponentPointer output)
{
    auto base = accessor;
    base.is_sparse = false;
    const auto components = cgltf_num_components(accessor.type);
    const auto read = [&](const cgltf_accessor& values, size_t row, size_t target)
    {
        return cgltf_accessor_read_float(&values, row, output(target), components) != 0;
    };
    for (size_t i = 0; i < count; ++i)
        if (!read(base, i, i))
            return false;
    return applySparse(accessor, read);
}

bool readInstanceTransforms(const cgltf_mesh_gpu_instancing& source, std::span<glm::mat4> matrices)
{
    std::vector<NodeTransform> transforms(matrices.size());
    std::vector<glm::vec4> rotations(matrices.size(), glm::vec4(0, 0, 0, 1));
    for (const auto& attribute : std::span(source.attributes, source.attributes_count))
    {
        const std::string_view name(attribute.name);
        const auto& accessor = *attribute.data;
        if (accessor.component_type != cgltf_component_type_r_32f)
            return false;
        if (name == "ROTATION")
        {
            if (accessor.type != cgltf_type_vec4 ||
                !readAccessor(accessor, matrices.size(), [&](size_t i)
                {
                    return glm::value_ptr(rotations[i]);
                }))
                return false;
        }
        else
        {
            if ((name != "TRANSLATION" && name != "SCALE") || accessor.type != cgltf_type_vec3)
                return false;
            const auto member =
                name == "TRANSLATION" ? &NodeTransform::translation : &NodeTransform::scale;
            if (!readAccessor(accessor, matrices.size(), [&](size_t i)
                {
                    return glm::value_ptr(transforms[i].*member);
                }))
                return false;
        }
    }
    for (size_t i = 0; i < matrices.size(); ++i)
    {
        const auto& q = rotations[i];
        const float length = glm::length(q);
        const auto& scale = transforms[i].scale;
        if (!std::isfinite(length) || length == 0.0f || double(scale.x) * scale.y * scale.z < 0.0)
            return false;
        transforms[i].rotation = glm::quat(q.w, q.x, q.y, q.z) / length;
        matrices[i] = transforms[i].matrix();
        if (!std::ranges::all_of(std::span(glm::value_ptr(matrices[i]), 16),
                                 [](float value) { return std::isfinite(value); }))
            return false;
    }
    return true;
}

const cgltf_extension* findExtension(std::span<const cgltf_extension> extensions,
                                     std::string_view name)
{
    const auto found = std::ranges::find_if(extensions,
                                            [name](const auto& extension)
                                            {
                                                return extension.name == name;
                                            });
    return found == extensions.end() ? nullptr : &*found;
}

std::optional<uint32_t> pointerIndex(std::string_view token)
{
    uint32_t result;
    const auto end = token.data() + token.size();
    const auto [next, error] = std::from_chars(token.data(), end, result);
    if (error != std::errc{} || next != end || (token.size() > 1 && token.front() == '0'))
        return std::nullopt;
    return result;
}

std::string xmpText(std::optional<GltfJsonValue> value, int depth = 0)
{
    if (!value || depth == 8)
        return {};
    if (value->isString)
    {
        std::string text(value->text);
        text.resize(cgltf_decode_string(text.data()));
        return text;
    }
    if (value->text.starts_with('['))
    {
        std::string text;
        for (const auto& item : gltfJsonArray(value->text))
        {
            const auto part = xmpText(item, depth + 1);
            if (part.empty())
                continue;
            if (!text.empty())
                text += ", ";
            text += part;
        }
        return text;
    }
    for (const auto* key : {"@value", "rdf:_1", "@list"})
        if (auto text = xmpText(gltfJsonProperty(value->text, key), depth + 1); !text.empty())
            return text;
    return {};
}

float jsonNumber(GltfJsonValue value)
{
    float number;
    const auto end = value.text.data() + value.text.size();
    const auto [next, error] = std::from_chars(value.text.data(), end, number);
    if (value.isString || error != std::errc{} || next != end || !std::isfinite(number))
        throw std::runtime_error("Invalid glTF extension number");
    return number;
}

template <glm::length_t N> glm::vec<N, float> jsonVector(GltfJsonValue value)
{
    const auto elements = gltfJsonArray(value.text);
    if (value.isString || elements.size() != N)
        throw std::runtime_error("Invalid glTF extension vector");
    glm::vec<N, float> result;
    for (glm::length_t i = 0; i < N; ++i)
        result[i] = jsonNumber(elements[i]);
    return result;
}

std::optional<AnimationBinding> animationBinding(std::string_view path)
{
    using enum AnimationTarget;
    const bool node = path.starts_with("/nodes/");
    if (!node && !path.starts_with("/materials/"))
        return std::nullopt;
    path.remove_prefix(node ? 7 : 11);
    const auto slash = path.find('/');
    if (slash == path.npos)
        return std::nullopt;
    const auto index = pointerIndex(path.substr(0, slash));
    if (!index)
        return std::nullopt;
    path.remove_prefix(slash + 1);
    if (node)
    {
        if (path == "extensions/KHR_node_visibility/visible")
            return AnimationBinding{Visibility, *index};
        return std::nullopt;
    }
    if (path == "pbrMetallicRoughness/baseColorFactor")
        return AnimationBinding{BaseColor, *index};
    constexpr std::string_view transform = "/extensions/KHR_texture_transform/";
    const auto extension = path.find(transform);
    if (extension == path.npos)
        return std::nullopt;
    constexpr std::string_view slots[]{
        "pbrMetallicRoughness/baseColorTexture",
        "normalTexture",
        "pbrMetallicRoughness/metallicRoughnessTexture",
        "occlusionTexture",
        "emissiveTexture",
        "extensions/KHR_materials_specular/specularTexture",
        "extensions/KHR_materials_specular/specularColorTexture",
        "extensions/KHR_materials_clearcoat/clearcoatTexture",
        "extensions/KHR_materials_clearcoat/clearcoatRoughnessTexture",
        "extensions/KHR_materials_clearcoat/clearcoatNormalTexture",
        "extensions/KHR_materials_sheen/sheenColorTexture",
        "extensions/KHR_materials_sheen/sheenRoughnessTexture",
        "extensions/KHR_materials_anisotropy/anisotropyTexture",
        "extensions/KHR_materials_iridescence/iridescenceTexture",
        "extensions/KHR_materials_iridescence/iridescenceThicknessTexture",
        "extensions/KHR_materials_transmission/transmissionTexture",
        "extensions/KHR_materials_volume/thicknessTexture",
        "extensions/KHR_materials_diffuse_transmission/diffuseTransmissionTexture",
        "extensions/KHR_materials_diffuse_transmission/diffuseTransmissionColorTexture"};
    static_assert(std::size(slots) == RetroreflectionSlot);
    for (uint32_t slot = 0; slot < std::size(slots); ++slot)
        if (path.substr(0, extension) == slots[slot])
        {
            const auto property = path.substr(extension + transform.size());
            if (property == "offset")
                return AnimationBinding{TextureOffset, *index, slot};
            if (property == "scale")
                return AnimationBinding{TextureScale, *index, slot};
            if (property == "rotation")
                return AnimationBinding{TextureRotation, *index, slot};
        }
    return std::nullopt;
}

bool readAnimationTrack(const cgltf_animation_sampler& sampler, AnimationTrack& track,
                        size_t morphCount)
{
    using enum AnimationTarget;
    const auto& input = *sampler.input;
    const auto& output = *sampler.output;
    const auto target = track.binding.target;
    const bool rotation = target == Rotation;
    const bool weights = target == Weights;
    switch (sampler.interpolation)
    {
    case cgltf_interpolation_type_linear:
        track.interpolation = AnimationInterpolation::Linear;
        break;
    case cgltf_interpolation_type_step:
        track.interpolation = AnimationInterpolation::Step;
        break;
    case cgltf_interpolation_type_cubic_spline:
        track.interpolation = AnimationInterpolation::CubicSpline;
        break;
    default:
        return false;
    }
    const bool cubic = track.interpolation == AnimationInterpolation::CubicSpline;
    const size_t stride = cubic ? 3 : 1;
    const auto component = output.component_type;
    const bool normalized = output.normalized &&
                            ((rotation && component == cgltf_component_type_r_16) ||
                             (weights && component == cgltf_component_type_r_8u));
    const bool componentValid =
        target == Visibility
            ? component == cgltf_component_type_r_8u &&
                  track.interpolation == AnimationInterpolation::Step
            : component == cgltf_component_type_r_32f || normalized;
    auto type = cgltf_type_scalar;
    if (rotation || target == BaseColor)
        type = cgltf_type_vec4;
    else if (target == Translation || target == Scale)
        type = cgltf_type_vec3;
    else if (target == TextureOffset || target == TextureScale)
        type = cgltf_type_vec2;
    const size_t valuesPerKey = weights ? morphCount : 1;
    if (input.type != cgltf_type_scalar || input.component_type != cgltf_component_type_r_32f ||
        input.count < (cubic ? 2u : 1u) || output.type != type || !componentValid ||
        !valuesPerKey || output.count % stride || output.count / stride % valuesPerKey ||
        output.count / stride / valuesPerKey != input.count)
        return false;

    track.times.resize(input.count);
    if (!readAccessor(input, input.count,
                      [&](size_t i)
                      {
                          return &track.times[i];
                      }))
        return false;
    float previous = -1.0f;
    for (float time : track.times)
    {
        if (!std::isfinite(time) || time < 0.0f || time <= previous)
            return false;
        previous = time;
    }
    if (weights)
    {
        track.weightValues.resize(output.count);
        if (!readAccessor(output, output.count,
                          [&](size_t i)
                          {
                              return &track.weightValues[i];
                          }))
            return false;
        return std::ranges::all_of(track.weightValues,
                                   [](float value) { return std::isfinite(value); });
    }
    track.values.resize(output.count, glm::vec4(0.0f));
    if (!readAccessor(output, output.count,
                      [&](size_t i)
                      {
                          return glm::value_ptr(track.values[i]);
                      }))
        return false;
    for (size_t i = 0; i < track.values.size(); ++i)
    {
        auto& value = track.values[i];
        if (normalized)
            value = glm::max(value, glm::vec4(-1.0f));
        for (glm::length_t c = 0; c < static_cast<glm::length_t>(cgltf_num_components(type)); ++c)
            if (!std::isfinite(value[c]))
                return false;
        if (rotation && (!cubic || i % 3 == 1))
        {
            const float length = glm::length(value);
            if (!std::isfinite(length) || length == 0.0f)
                return false;
            value /= length;
        }
    }
    return true;
}

draco::DataType dracoDataType(cgltf_component_type type)
{
    switch (type)
    {
    case cgltf_component_type_r_16u:
        return draco::DT_UINT16;
    case cgltf_component_type_r_32f:
        return draco::DT_FLOAT32;
    default:
        return draco::DT_INVALID;
    }
}

struct PrimitiveData
{
    const cgltf_primitive& source;
    std::unique_ptr<draco::Mesh> mesh;

    bool decode(size_t vertexCount, size_t indexCount)
    {
        if (!source.has_draco_mesh_compression)
            return true;
        const auto* view = source.draco_mesh_compression.buffer_view;
        if (!view || !view->buffer || view->offset > view->buffer->size || !view->size ||
            view->size > view->buffer->size - view->offset)
            return false;
        const auto* bytes = cgltf_buffer_view_data(view);
        if (!bytes)
            return false;
        draco::DecoderBuffer buffer;
        buffer.Init(reinterpret_cast<const char*>(bytes), view->size);
        auto decoded = draco::Decoder().DecodeMeshFromBuffer(&buffer);
        if (!decoded.ok())
            return false;
        mesh = std::move(decoded).value();
        return mesh && mesh->num_points() == vertexCount && mesh->num_faces() == indexCount / 3;
    }

    bool readIndices(std::span<uint32_t> indices) const
    {
        const auto* accessor = source.indices;
        if (!mesh)
        {
            if (accessor)
                return cgltf_accessor_unpack_indices(accessor, indices.data(), sizeof(uint32_t),
                                                     indices.size()) == indices.size();
            std::iota(indices.begin(), indices.end(), 0u);
            return true;
        }
        if (!accessor || (accessor->component_type != cgltf_component_type_r_16u &&
                          accessor->component_type != cgltf_component_type_r_32u))
            return false;
        const auto limit = accessor->component_type == cgltf_component_type_r_16u ? UINT16_MAX
                                                                                 : UINT32_MAX;
        for (uint32_t i = 0; i < mesh->num_faces(); ++i)
        {
            const auto& face = mesh->face(draco::FaceIndex(i));
            for (size_t corner = 0; corner < 3; ++corner)
            {
                const auto index = face[corner].value();
                if (index > limit)
                    return false;
                indices[static_cast<size_t>(i) * 3 + corner] = index;
            }
        }
        return true;
    }

    template <typename ComponentPointer>
    bool read(const cgltf_accessor& accessor, cgltf_attribute_type type, int set,
              ComponentPointer output) const
    {
        const auto components = cgltf_num_components(accessor.type);
        const bool signedNormalized =
            accessor.normalized && (accessor.component_type == cgltf_component_type_r_8 ||
                                    accessor.component_type == cgltf_component_type_r_16);
        const auto& extension = source.draco_mesh_compression;
        const auto mappings = std::span(extension.attributes, extension.attributes_count);
        const auto mapping =
            std::ranges::find_if(mappings,
                                 [&](const cgltf_attribute& attribute)
                                 {
                                     return attribute.type == type && attribute.index == set;
                                 });
        if (mesh && mapping != mappings.end())
        {
            const auto* attribute = mesh->GetAttributeByUniqueId(mapping->draco_id);
            if (!attribute || attribute->num_components() != components ||
                attribute->data_type() != dracoDataType(accessor.component_type))
                return false;
            draco::GeometryAttribute values = *attribute;
            values.set_normalized(accessor.normalized != 0);
            for (uint32_t i = 0; i < accessor.count; ++i)
            {
                const auto index = attribute->mapped_index(draco::PointIndex(i));
                if (index.value() >= attribute->size() ||
                    !values.ConvertValue(index, static_cast<int8_t>(components), output(i)))
                    return false;
            }
        }
        else if (!readAccessor(accessor, accessor.count, output))
            return false;

        if (signedNormalized)
            for (size_t i = 0; i < accessor.count; ++i)
                for (auto& value : std::span(output(i), components))
                    value = std::max(value, -1.0f);
        return true;
    }

    template <typename Vector>
    bool read(const cgltf_accessor* accessor, cgltf_attribute_type type, std::span<Vertex> vertices,
              Vector Vertex::* member) const
    {
        return !accessor || read(*accessor, type, 0,
                                 [&](size_t i)
                                 {
                                     return glm::value_ptr(vertices[i].*member);
                                 });
    }
};

bool readSkinInfluences(const PrimitiveData& geometry, std::span<Vertex> vertices,
                        size_t jointCount)
{
    const auto* joints = cgltf_find_accessor(&geometry.source, cgltf_attribute_type_joints, 0);
    const auto* weights = cgltf_find_accessor(&geometry.source, cgltf_attribute_type_weights, 0);
    if (!joints || !weights || joints->type != cgltf_type_vec4 ||
        weights->type != cgltf_type_vec4 || joints->normalized ||
        (joints->component_type != cgltf_component_type_r_8u &&
         joints->component_type != cgltf_component_type_r_16u))
        return false;
    if (weights->component_type != cgltf_component_type_r_32f &&
        !(weights->normalized && (weights->component_type == cgltf_component_type_r_8u ||
                                  weights->component_type == cgltf_component_type_r_16u)))
        return false;

    std::vector<glm::vec4> indices(vertices.size());
    if (!geometry.read(*joints, cgltf_attribute_type_joints, 0,
                       [&](size_t i)
                       {
                           return glm::value_ptr(indices[i]);
                       }) ||
        !geometry.read(weights, cgltf_attribute_type_weights, vertices, &Vertex::weights))
        return false;
    for (size_t i = 0; i < vertices.size(); ++i)
    {
        auto& vertex = vertices[i];
        for (int c = 0; c < 4; ++c)
        {
            const auto index = static_cast<uint16_t>(indices[i][c]);
            const float weight = vertex.weights[c];
            if (index >= jointCount || !std::isfinite(weight) || weight < 0.0f || weight > 1.0f)
                return false;
            vertex.joints[c] = index;
        }
        const float sum = glm::dot(vertex.weights, glm::vec4(1.0f));
        if (sum <= 0.0f)
            return false;
        vertex.weights /= sum;
    }
    return true;
}

template <typename Visit> void forEachInfluence(const Vertex& vertex, Visit visit)
{
    for (int c = 0; c < 4; ++c)
        if (const auto weight = vertex.weights[c]; weight > 0.0f)
            visit(vertex.joints[c], weight);
}

bool readMorphTargets(const cgltf_primitive& primitive, std::span<Vertex> vertices,
                      std::vector<MorphDelta>& deltas, std::span<ModelBounds> bounds)
{
    const auto count = bounds.size();
    if (primitive.targets_count != count)
        return false;
    if (!count)
        return true;
    const auto first = deltas.size();
    if (vertices.size() > (UINT32_MAX - first) / count)
        return false;
    deltas.resize(first + vertices.size() * count);
    for (size_t vertex = 0; vertex < vertices.size(); ++vertex)
        vertices[vertex].morph = {static_cast<uint32_t>(first + vertex * count),
                                  static_cast<uint32_t>(count)};

    for (size_t target = 0; target < count; ++target)
    {
        const auto& source = primitive.targets[target];
        for (size_t i = 0; i < source.attributes_count; ++i)
        {
            const auto& attribute = source.attributes[i];
            glm::vec4 MorphDelta::* member = nullptr;
            if (attribute.type == cgltf_attribute_type_position)
                member = &MorphDelta::position;
            else if (attribute.type == cgltf_attribute_type_normal)
                member = &MorphDelta::normal;
            else if (attribute.type == cgltf_attribute_type_tangent)
                member = &MorphDelta::tangent;
            if (!member)
                continue;
            const auto& accessor = *attribute.data;
            if (accessor.count != vertices.size() || accessor.type != cgltf_type_vec3 ||
                (accessor.component_type != cgltf_component_type_r_32f &&
                 accessor.component_type != cgltf_component_type_r_8 &&
                 accessor.component_type != cgltf_component_type_r_16) ||
                !readAccessor(accessor, vertices.size(),
                              [&](size_t vertex)
                              {
                                  return glm::value_ptr(deltas[first + vertex * count + target].*
                                                        member);
                              }))
                return false;
            for (size_t vertex = 0; vertex < vertices.size(); ++vertex)
            {
                auto& delta = deltas[first + vertex * count + target].*member;
                for (int axis = 0; axis < 3; ++axis)
                {
                    if (!std::isfinite(delta[axis]))
                        return false;
                    if (accessor.normalized)
                        delta[axis] = std::max(delta[axis], -1.0f);
                }
            }
        }
        for (size_t vertex = 0; vertex < vertices.size(); ++vertex)
        {
            const glm::vec3 delta(deltas[first + vertex * count + target].position);
            bounds[target].minimum = glm::min(bounds[target].minimum, delta);
            bounds[target].maximum = glm::max(bounds[target].maximum, delta);
        }
    }
    return true;
}

glm::vec3 restPosition(const Model& model, const Draw& draw, const Vertex& vertex)
{
    const auto& scene = model.scene;
    const auto offset = scene.nodes[draw.node].morphOffset;
    glm::vec4 position(vertex.position, 1.0f);
    for (uint32_t target = 0; target < vertex.morph[1]; ++target)
        position += model.morphDeltas[vertex.morph[0] + target].position *
                    scene.restWeights[offset + target];
    if (draw.skin == noSkin)
        return glm::vec3(scene.restWorld[draw.node] * position);
    glm::vec3 result(0.0f);
    forEachInfluence(vertex,
                     [&](uint16_t joint, float weight)
                     {
                         result +=
                             weight * glm::vec3(scene.restJoints[draw.skin + joint] * position);
                     });
    return result;
}

TextureSampler readSampler(const cgltf_sampler* source)
{
    TextureSampler sampler;
    if (!source)
        return sampler;
    const auto wrap = [](cgltf_wrap_mode mode)
    {
        switch (mode)
        {
        case cgltf_wrap_mode_mirrored_repeat:
            return TextureWrap::MirroredRepeat;
        case cgltf_wrap_mode_clamp_to_edge:
            return TextureWrap::ClampToEdge;
        default:
            return TextureWrap::Repeat;
        }
    };
    sampler.wrapU = wrap(source->wrap_s);
    sampler.wrapV = wrap(source->wrap_t);
    sampler.nearestMag = source->mag_filter == cgltf_filter_type_nearest;
    switch (source->min_filter)
    {
    case cgltf_filter_type_linear:
        sampler.mip = MipFilter::None;
        break;
    case cgltf_filter_type_linear_mipmap_nearest:
        sampler.mip = MipFilter::Nearest;
        break;
    default:
        break;
    }
    sampler.nearestMin = source->min_filter == cgltf_filter_type_nearest_mipmap_linear;
    return sampler;
}

template <typename Output>
bool readTextureCoordinates(const PrimitiveData& geometry, const cgltf_texture_view& view,
                            size_t count, bool animated, Output output)
{
    const auto& transform = view.transform;
    const int uvSet =
        view.has_transform && transform.has_texcoord ? transform.texcoord : view.texcoord;
    const auto* accessor =
        uvSet >= 0 ? cgltf_find_accessor(&geometry.source, cgltf_attribute_type_texcoord, uvSet)
                   : nullptr;
    if (!accessor || accessor->type != cgltf_type_vec2 ||
        !geometry.read(*accessor, cgltf_attribute_type_texcoord, uvSet, output))
        return false;
    glm::vec2 u(1, 0), v(0, 1), offset(0);
    if (view.has_transform && !animated)
    {
        const float c = std::cos(transform.rotation), s = std::sin(transform.rotation);
        // Positive rotation is counterclockwise with V pointing down.
        u = {c * transform.scale[0], -s * transform.scale[0]};
        v = {s * transform.scale[1], c * transform.scale[1]};
        offset = glm::make_vec2(transform.offset);
    }
    for (size_t i = 0; i < count; ++i)
    {
        auto* uv = output(i);
        const auto point = offset + u * uv[0] + v * uv[1];
        if (!std::isfinite(point.x) || !std::isfinite(point.y))
            return false;
        uv[0] = point.x;
        uv[1] = point.y;
    }
    return true;
}

bool readAdvancedMaterial(const cgltf_data& data, const cgltf_material& source, Material& material,
                          std::array<cgltf_texture_view, MaterialTextureCount>& views)
{
    auto& advanced = material.advanced;
    if (source.has_pbr_specular_glossiness)
    {
        const auto& pbr = source.pbr_specular_glossiness;
        material.baseColorFactor = glm::make_vec4(pbr.diffuse_factor);
        advanced.specular = glm::vec4(glm::make_vec3(pbr.specular_factor), 1.0f);
        advanced.workflow.y = pbr.glossiness_factor;
        advanced.workflow.z = 1.0f;
        views[BaseColorSlot] = pbr.diffuse_texture;
        views[MetallicRoughnessSlot] = pbr.specular_glossiness_texture;
        return true;
    }
    if (source.has_ior)
        advanced.workflow.x = source.ior.ior;
    if (source.has_specular)
    {
        const auto& specular = source.specular;
        advanced.specular =
            glm::vec4(glm::make_vec3(specular.specular_color_factor), specular.specular_factor);
        views[SpecularSlot] = specular.specular_texture;
        views[SpecularColorSlot] = specular.specular_color_texture;
    }
    if (source.has_clearcoat)
    {
        const auto& coat = source.clearcoat;
        advanced.clearcoat = {
            coat.clearcoat_factor, coat.clearcoat_roughness_factor,
            coat.clearcoat_normal_texture.texture ? coat.clearcoat_normal_texture.scale : 0.0f,
            0.0f};
        views[ClearcoatSlot] = coat.clearcoat_texture;
        views[ClearcoatRoughnessSlot] = coat.clearcoat_roughness_texture;
        views[ClearcoatNormalSlot] = coat.clearcoat_normal_texture;
    }
    if (source.has_sheen)
    {
        const auto& sheen = source.sheen;
        advanced.sheen =
            glm::vec4(glm::make_vec3(sheen.sheen_color_factor), sheen.sheen_roughness_factor);
        views[SheenColorSlot] = sheen.sheen_color_texture;
        views[SheenRoughnessSlot] = sheen.sheen_roughness_texture;
    }
    if (source.has_anisotropy)
    {
        const auto& anisotropy = source.anisotropy;
        advanced.anisotropy = {anisotropy.anisotropy_strength,
                               std::cos(anisotropy.anisotropy_rotation),
                               std::sin(anisotropy.anisotropy_rotation),
                               anisotropy.anisotropy_texture.texture ? 1.0f : 0.0f};
        views[AnisotropySlot] = anisotropy.anisotropy_texture;
    }
    if (source.has_iridescence)
    {
        const auto& film = source.iridescence;
        advanced.iridescence = {film.iridescence_factor, film.iridescence_ior,
                                film.iridescence_thickness_min, film.iridescence_thickness_max};
        views[IridescenceSlot] = film.iridescence_texture;
        views[IridescenceThicknessSlot] = film.iridescence_thickness_texture;
    }
    if (source.has_transmission)
    {
        advanced.transmission.x = source.transmission.transmission_factor;
        views[TransmissionSlot] = source.transmission.transmission_texture;
    }
    if (source.has_volume)
    {
        const auto& volume = source.volume;
        advanced.transmission.y = volume.thickness_factor;
        const float distance = volume.attenuation_distance == std::numeric_limits<float>::max()
                                   ? 0.0f
                                   : volume.attenuation_distance;
        advanced.attenuation = glm::vec4(glm::make_vec3(volume.attenuation_color), distance);
        views[ThicknessSlot] = volume.thickness_texture;
    }
    if (source.has_dispersion)
        advanced.transmission.z = source.dispersion.dispersion;
    if (source.has_diffuse_transmission)
    {
        const auto& diffuse = source.diffuse_transmission;
        advanced.diffuseTransmission =
            glm::vec4(glm::make_vec3(diffuse.diffuse_transmission_color_factor),
                      diffuse.diffuse_transmission_factor);
        views[DiffuseTransmissionSlot] = diffuse.diffuse_transmission_texture;
        views[DiffuseTransmissionColorSlot] = diffuse.diffuse_transmission_color_texture;
    }
    const std::span extensions(source.extensions, source.extensions_count);
    const auto* volumeScatter = findExtension(extensions, "KHR_materials_volume_scatter");
    const auto* retro = findExtension(extensions, "KHR_materials_retroreflection");
    if (volumeScatter)
    {
        const auto color = gltfJsonProperty(volumeScatter->data, "multiscatterColorFactor");
        advanced.scattering = glm::vec4(color ? jsonVector<3>(*color) : glm::vec3(1), 0);
    }
    if (retro)
    {
        if (const auto factor = gltfJsonProperty(retro->data, "retroreflectionFactor"))
            advanced.workflow.w = jsonNumber(*factor);
        if (const auto map = gltfJsonProperty(retro->data, "retroreflectionTexture"))
            if (map->isString || !gltfTextureView(map->text, data, views[RetroreflectionSlot]))
                throw std::runtime_error("Invalid glTF retroreflection texture");
    }
    return source.has_ior || source.has_specular || source.has_clearcoat || source.has_sheen ||
           source.has_anisotropy || source.has_iridescence || source.has_transmission ||
           source.has_volume || source.has_dispersion || source.has_diffuse_transmission ||
           volumeScatter || retro;
}

bool generateFlatNormals(std::vector<Vertex>& vertices, size_t base, std::span<uint32_t> indices,
                         bool indexed)
{
    if (indexed)
    {
        if (indices.size() > INT32_MAX - base)
            return false;
        const auto original = std::span(vertices).subspan(base);
        const std::vector<Vertex> source(original.begin(), original.end());
        vertices.resize(base + indices.size());
        for (size_t i = 0; i < indices.size(); ++i)
            vertices[base + i] = source[indices[i]];
        std::iota(indices.begin(), indices.end(), 0u);
    }

    for (size_t i = 0; i < indices.size(); i += 3)
    {
        auto& a = vertices[base + i];
        auto& b = vertices[base + i + 1];
        auto& c = vertices[base + i + 2];
        const auto area = glm::cross(glm::dvec3(b.position) - glm::dvec3(a.position),
                                     glm::dvec3(c.position) - glm::dvec3(a.position));
        const auto length = glm::length(area);
        const auto normal = length > 0.0 ? glm::vec3(area / length) : glm::vec3(0, 1, 0);
        a.normal = b.normal = c.normal = normal;
    }
    return true;
}

struct TangentMesh
{
    std::span<const Vertex> vertices;
    std::span<const uint32_t> indices;
    std::span<glm::vec4> tangents;
    float authoredHandednessScale;

    static const TangentMesh& from(const SMikkTSpaceContext* context)
    {
        return *static_cast<const TangentMesh*>(context->m_pUserData);
    }

    const Vertex& vertex(int face, int corner) const
    {
        return vertices[indices[face * 3 + corner]];
    }
};

bool generateTangents(std::vector<Vertex>& vertices, size_t base, std::span<uint32_t> indices,
                      float authoredHandednessScale)
{
    // MikkTSpace packs a face index and two corner bits into an int.
    if (indices.size() / 3 > std::numeric_limits<int>::max() / 4)
        return false;
    std::vector<glm::vec4> tangents(indices.size());
    const auto sourceVertices = std::span(vertices).subspan(base);
    TangentMesh mesh{sourceVertices, indices, tangents, authoredHandednessScale};
    SMikkTSpaceInterface interface{
        .m_getNumFaces =
            [](const SMikkTSpaceContext* context)
        {
            return static_cast<int>(TangentMesh::from(context).indices.size() / 3);
        },
        .m_getNumVerticesOfFace =
            [](const SMikkTSpaceContext*, int)
        {
            return 3;
        },
        .m_getPosition =
            [](const SMikkTSpaceContext* context, float* out, int face, int corner)
        {
            std::copy_n(glm::value_ptr(TangentMesh::from(context).vertex(face, corner).position), 3,
                        out);
        },
        .m_getNormal =
            [](const SMikkTSpaceContext* context, float* out, int face, int corner)
        {
            std::copy_n(glm::value_ptr(TangentMesh::from(context).vertex(face, corner).normal), 3,
                        out);
        },
        .m_getTexCoord =
            [](const SMikkTSpaceContext* context, float* out, int face, int corner)
        {
            std::copy_n(
                glm::value_ptr(TangentMesh::from(context).vertex(face, corner).uv[NormalSlot]), 2,
                out);
        },
        .m_setTSpaceBasic =
            [](const SMikkTSpaceContext* context, const float* tangent, float sign, int face,
               int corner)
        {
            const auto& mesh = TangentMesh::from(context);
            if (mesh.authoredHandednessScale != 0.0f)
                sign = mesh.vertex(face, corner).tangent.w * mesh.authoredHandednessScale;
            mesh.tangents[face * 3 + corner] = glm::vec4(tangent[0], tangent[1], tangent[2], sign);
        }};
    const SMikkTSpaceContext context{&interface, &mesh};
    if (!genTangSpaceDefault(&context))
        return false;
    for (auto& vertex : sourceVertices)
        vertex.tangent.w = 0.0f;

    // The same input vertex may have multiple corner tangents at a UV seam.
    constexpr uint32_t noVertex = UINT32_MAX;
    std::vector<uint32_t> nextVertex(vertices.size() - base, noVertex);
    for (size_t corner = 0; corner < indices.size(); ++corner)
    {
        auto index = indices[corner];
        auto tangent = tangents[corner];
        const auto normal = vertices[base + index].normal;
        if (glm::length(glm::cross(normal, glm::vec3(tangent))) <= 1e-6f)
        {
            // Degenerate UVs can leave an arbitrary tangent parallel to the normal.
            const auto axis = std::abs(normal.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
            tangent = glm::vec4(glm::normalize(glm::cross(normal, axis)), tangent.w);
        }
        while (vertices[base + index].tangent.w != 0.0f &&
               vertices[base + index].tangent != tangent)
        {
            if (nextVertex[index] != noVertex)
            {
                index = nextVertex[index];
                continue;
            }
            if (vertices.size() == INT32_MAX)
                return false;
            nextVertex[index] = static_cast<uint32_t>(vertices.size() - base);
            vertices.push_back(vertices[base + index]);
            index = nextVertex[index];
            nextVertex.push_back(noVertex);
            break;
        }
        vertices[base + index].tangent = tangent;
        indices[corner] = index;
    }
    return true;
}
} // namespace

std::filesystem::path copyModel(const std::filesystem::path& source,
                                const std::filesystem::path& directory,
                                const ImportProgress& progress)
{
    namespace fs = std::filesystem;
    auto extension = pathUtf8(source.extension());
    std::ranges::transform(extension, extension.begin(),
                           [](unsigned char c)
                           {
                               return static_cast<char>(std::tolower(c));
                           });
    if (extension != ".gltf" && extension != ".glb")
        throw std::runtime_error("Choose a .gltf or .glb model.");
    if (progress)
        progress(ImportStage::Copying);
    const auto data = parseGltf(fileOptions(), pathUtf8(source));
    const auto expected = extension == ".glb" ? cgltf_file_type_glb : cgltf_file_type_gltf;
    if (data->file_type != expected)
        throw std::runtime_error("Model contents do not match the file extension.");
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
    const auto imported = destination / source.filename();
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
            if (progress)
                progress(ImportStage::Copying);
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
        if (progress)
            progress(ImportStage::Copying);
        fs::copy_file(source, pending / imported.filename());
        for (int attempt = 0;; ++attempt)
        {
            std::error_code error;
            fs::rename(pending, destination, error);
            if (!error)
                break;
            // A file scanner can hold a new import open briefly on Windows.
            if (attempt == 4 || (error != std::errc::permission_denied &&
                                 error != std::errc::device_or_resource_busy))
                throw fs::filesystem_error("Finalize copied model", pending, destination, error);
            std::this_thread::sleep_for(std::chrono::milliseconds(25 * (attempt + 1)));
            if (progress)
                progress(ImportStage::Copying);
        }
    }
    catch (...)
    {
        std::error_code ignored;
        fs::remove_all(pending, ignored);
        throw;
    }
    return imported;
}

Model loadModel(const std::filesystem::path& path, const ImportProgress& progress)
{
    if (progress)
        progress(ImportStage::Reading);
    const auto filename = pathUtf8(path);
    const auto require = [&](bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error("Cannot load model: " + filename + " (" + message + ")");
    };
    const auto options = fileOptions();
    const auto data = std::shared_ptr<cgltf_data>(parseGltf(options, filename));
    if (progress)
        progress(ImportStage::Reading);
    require(cgltf_load_buffers(&options, data.get(), filename.c_str()) == cgltf_result_success,
            "buffer load failed");
    require(cgltf_validate(data.get()) == cgltf_result_success, "invalid glTF data");
    if (progress)
        progress(ImportStage::BuildingMeshes);
    bool pointerRequired = false;
    for (cgltf_size i = 0; i < data->extensions_required_count; ++i)
    {
        const std::string_view extension = data->extensions_required[i];
        pointerRequired |= extension == "KHR_animation_pointer";
        if (extension != "KHR_materials_emissive_strength" &&
            extension != "KHR_texture_transform" && extension != "KHR_materials_unlit" &&
            extension != "KHR_mesh_quantization" && extension != "KHR_draco_mesh_compression" &&
            extension != "KHR_meshopt_compression" && extension != "EXT_meshopt_compression" &&
            extension != "EXT_mesh_gpu_instancing" &&
            extension != "KHR_texture_basisu" && extension != "EXT_texture_webp" &&
            extension != "KHR_node_visibility" && extension != "KHR_animation_pointer" &&
            extension != "KHR_materials_specular" && extension != "KHR_materials_ior" &&
            extension != "KHR_materials_clearcoat" && extension != "KHR_materials_sheen" &&
            extension != "KHR_materials_anisotropy" && extension != "KHR_materials_iridescence" &&
            extension != "KHR_materials_pbrSpecularGlossiness" &&
            extension != "KHR_materials_transmission" && extension != "KHR_materials_volume" &&
            extension != "KHR_materials_dispersion" && extension != "KHR_materials_variants" &&
            extension != "KHR_xmp_json_ld" && extension != "KHR_xmp" &&
            extension != "EXT_lights_image_based" && extension != "KHR_lights_punctual" &&
            extension != "KHR_materials_diffuse_transmission" &&
            extension != "KHR_materials_volume_scatter" &&
            extension != "KHR_materials_retroreflection")
            throw std::runtime_error(std::string("Unsupported required glTF extension: ") +
                                     data->extensions_required[i]);
    }

    for (auto& view : std::span(data->buffer_views, data->buffer_views_count))
        if (view.has_meshopt_compression)
        {
            if (progress)
                progress(ImportStage::BuildingMeshes);
            require(decodeMeshoptView(*data, view), "invalid Meshopt buffer view");
        }

    std::unordered_map<const cgltf_animation_channel*, AnimationBinding> pointerTargets;
    std::vector<uint32_t> animatedUvSlots(data->materials_count);
    for (const auto& animation : std::span(data->animations, data->animations_count))
        for (const auto& channel : std::span(animation.channels, animation.channels_count))
        {
            const auto* extension = findExtension({channel.extensions, channel.extensions_count},
                                                  "KHR_animation_pointer");
            if (!extension)
                continue;
            const auto token = gltfJsonProperty(extension->data, "pointer");
            require(!channel.target_node &&
                        channel.target_path == cgltf_animation_path_type_invalid && token &&
                        token->isString,
                    "invalid KHR_animation_pointer target");
            std::string pointer(token->text);
            pointer.resize(cgltf_decode_string(pointer.data()));
            const auto binding = animationBinding(pointer);
            if (!binding)
            {
                if (pointerRequired)
                    throw std::runtime_error("Unsupported glTF animation pointer: " + pointer);
                continue;
            }
            const bool material = binding->target >= AnimationTarget::BaseColor;
            require(binding->index < (material ? data->materials_count : data->nodes_count),
                    "animation pointer index out of range");
            pointerTargets.emplace(&channel, *binding);
            if (binding->target >= AnimationTarget::TextureOffset)
                animatedUvSlots[binding->index] |= 1u << binding->element;
        }

    Model model;
    for (const auto& variant : std::span(data->variants, data->variants_count))
        model.scene.materialVariants.emplace_back(variant.name ? variant.name : "Variant");
    const std::span assetExtensions(data->asset.extensions, data->asset.extensions_count);
    const auto* xmp = findExtension(assetExtensions, "KHR_xmp_json_ld");
    if (!xmp)
        xmp = findExtension(assetExtensions, "KHR_xmp");
    if (xmp)
    {
        const auto* root =
            findExtension({data->data_extensions, data->data_extensions_count}, xmp->name);
        const auto reference = gltfJsonProperty(xmp->data, "packet");
        const auto index =
            reference && !reference->isString ? pointerIndex(reference->text) : std::nullopt;
        const auto packets = root ? gltfJsonProperty(root->data, "packets") : std::nullopt;
        const auto list = packets ? gltfJsonArray(packets->text) : std::vector<GltfJsonValue>{};
        require(index && *index < list.size() && list[*index].text.starts_with('{'),
                "invalid asset XMP packet");
        const auto json = list[*index].text;
        const auto field = [json](std::string_view key)
        { return xmpText(gltfJsonProperty(json, key)); };
        XmpMetadata metadata{field("dc:title"), field("dc:creator"), field("dc:description")};
        if (!metadata.title.empty() || !metadata.author.empty() || !metadata.description.empty())
            model.xmp = std::move(metadata);
    }
    std::vector<EncodedImage> embeddedImages(data->images_count);
    const auto imageSource = [&](const cgltf_image* image, TextureSource source)
    {
        require(image, "texture has no supported image source");
        const std::string_view uri = image->uri ? image->uri : "";
        if (image->buffer_view || uri.starts_with("data:"))
        {
            auto& encoded = embeddedImages[cgltf_image_index(data.get(), image)];
            if (!encoded.data)
            {
                if (const auto* buffer = image->buffer_view)
                {
                    require(uri.empty() && buffer->buffer &&
                                buffer->offset <= buffer->buffer->size &&
                                buffer->size <= buffer->buffer->size - buffer->offset,
                            "invalid image buffer view");
                    const auto* bytes = cgltf_buffer_view_data(buffer);
                    require(bytes && buffer->size && buffer->size <= INT32_MAX,
                            "invalid image buffer view");
                    encoded = {std::shared_ptr<const uint8_t>(data, bytes), buffer->size};
                }
                else
                {
                    encoded = decodeDataImage(options, uri);
                    require(encoded.data && encoded.size && encoded.size <= INT32_MAX,
                            "invalid image data URI");
                }
            }
            source.path = path;
            source.embedded = encoded;
        }
        else
        {
            require(!uri.empty(), "texture has no image data");
            std::string decoded(uri);
            decoded.resize(cgltf_decode_uri(decoded.data()));
            source.path = (path.parent_path() / utf8Path(decoded)).lexically_normal();
        }
        return source;
    };
    const auto getTexture = [&](const cgltf_texture_view& view, bool srgb, bool flatNormal = false)
    {
        TextureSource source{.srgb = srgb, .flatNormal = flatNormal};
        if (view.texture)
        {
            const auto& texture = *view.texture;
            const auto* image = texture.image;
            if (texture.basisu_image)
            {
                image = texture.basisu_image;
                source.encoding = TextureEncoding::BasisKtx2;
            }
            else if (texture.webp_image)
            {
                image = texture.webp_image;
                source.encoding = TextureEncoding::WebP;
            }
            source = imageSource(image, source);
        }
        const auto found = std::find(model.textures.begin(), model.textures.end(), source);
        const auto index = static_cast<uint32_t>(found - model.textures.begin());
        if (found == model.textures.end())
            model.textures.push_back(source);
        return index;
    };
    std::vector<const cgltf_material*> materialSources;
    std::vector<std::array<cgltf_texture_view, MaterialTextureCount>> materialViews;
    std::vector<uint32_t> materialUvSlots;
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
            material.unlit = source->unlit;
        }
        require(material.alphaMode != AlphaMode::Mask ||
                    (std::isfinite(material.alphaCutoff) && material.alphaCutoff >= 0.0f),
                "invalid alpha cutoff");
        std::array<cgltf_texture_view, MaterialTextureCount> views{};
        if (source && source->has_pbr_metallic_roughness)
        {
            material.baseColorFactor =
                glm::make_vec4(source->pbr_metallic_roughness.base_color_factor);
            views[BaseColorSlot] = source->pbr_metallic_roughness.base_color_texture;
        }
        if (source && !material.unlit)
        {
            if (source->has_pbr_metallic_roughness)
            {
                views[MetallicRoughnessSlot] =
                    source->pbr_metallic_roughness.metallic_roughness_texture;
                material.metallicFactor = source->pbr_metallic_roughness.metallic_factor;
                material.roughnessFactor = source->pbr_metallic_roughness.roughness_factor;
            }
            views[NormalSlot] = source->normal_texture;
            views[OcclusionSlot] = source->occlusion_texture;
            views[EmissiveSlot] = source->emissive_texture;
            material.emissiveFactor = glm::make_vec3(source->emissive_factor);
            if (source->has_emissive_strength)
                material.emissiveStrength = source->emissive_strength.emissive_strength;
        }
        const bool advanced =
            source && !material.unlit && readAdvancedMaterial(*data, *source, material, views);
        const auto& optical = material.advanced.transmission;
        require(std::isfinite(optical.x) && optical.x >= 0.0f && optical.x <= 1.0f &&
                    std::isfinite(optical.y) && optical.y >= 0.0f && std::isfinite(optical.z) &&
                    optical.z >= 0.0f,
                "invalid transmission, thickness or dispersion factor");
        const auto& attenuation = material.advanced.attenuation;
        const bool hasVolume = source && source->has_volume && advanced && !material.unlit;
        require(std::isfinite(attenuation.w) &&
                    (!hasVolume || source->volume.attenuation_distance > 0.0f),
                "invalid attenuation distance");
        for (int i = 0; i < 3; ++i)
            require(std::isfinite(attenuation[i]) && attenuation[i] >= 0.0f &&
                        attenuation[i] <= 1.0f,
                    "invalid attenuation color");
        uint32_t uvSlots = source ? animatedUvSlots[cgltf_material_index(data.get(), source)] : 0;
        for (size_t slot = 0; slot < views.size(); ++slot)
        {
            const auto& view = views[slot];
            const bool srgb = slot == BaseColorSlot || slot == EmissiveSlot ||
                              slot == SpecularColorSlot || slot == SheenColorSlot ||
                              slot == DiffuseTransmissionColorSlot ||
                              (slot == MetallicRoughnessSlot && material.advanced.workflow.z != 0);
            material.textures[slot] = {
                getTexture(view, srgb, slot == NormalSlot || slot == ClearcoatNormalSlot),
                readSampler(view.texture ? view.texture->sampler : nullptr)};
            const auto flag = 1u << slot;
            if (!view.texture || !view.has_transform)
                uvSlots &= ~flag;
            if (uvSlots & flag)
            {
                auto& transform = material.uv[slot];
                transform = {glm::make_vec2(view.transform.offset),
                             glm::make_vec2(view.transform.scale), view.transform.rotation};
                require(std::isfinite(transform.offset.x) && std::isfinite(transform.offset.y) &&
                            std::isfinite(transform.scale.x) && std::isfinite(transform.scale.y) &&
                            std::isfinite(transform.rotation),
                        "invalid texture transform");
            }
        }
        require(std::isfinite(material.emissiveStrength) && material.emissiveStrength >= 0.0f,
                "invalid emissive strength");
        for (int component = 0; component < 3; ++component)
            require(std::isfinite(material.emissiveFactor[component]) &&
                        material.emissiveFactor[component] >= 0.0f &&
                        material.emissiveFactor[component] <= 1.0f,
                    "invalid emissive factor");
        if (views[NormalSlot].texture)
            material.normalScale = views[NormalSlot].scale;
        if (views[OcclusionSlot].texture)
            material.occlusionStrength = views[OcclusionSlot].scale;
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
        materialViews.push_back(views);
        materialUvSlots.push_back(uvSlots);
        material.animatedUv = uvSlots != 0;
        if (advanced || material.animatedUv)
            material.gpuIndex = ++model.scene.gpuMaterialCount;
        model.materials.push_back(material);
        model.scene.restMaterials.push_back(material);
        return static_cast<uint32_t>(model.materials.size() - 1);
    };

    const auto* scene =
        data->scene ? data->scene : (data->scenes_count ? &data->scenes[0] : nullptr);
    require(scene != nullptr, "no scene");
    constexpr std::string_view ibl = "EXT_lights_image_based";
    if (const auto* extension = findExtension({scene->extensions, scene->extensions_count}, ibl))
    {
        const auto index = [](GltfJsonValue value)
        {
            return value.isString ? std::optional<uint32_t>{} : pointerIndex(value.text);
        };
        const auto property = [](std::string_view object, std::string_view key)
        {
            const auto value = gltfJsonProperty(object, key);
            if (!value)
                throw std::runtime_error("Missing image-based light property: " + std::string(key));
            return *value;
        };
        const auto* root = findExtension({data->data_extensions, data->data_extensions_count}, ibl);
        require(root, "missing image-based light definitions");
        const auto lights = gltfJsonArray(property(root->data, "lights").text);
        const auto selected = index(property(extension->data, "light"));
        require(selected && *selected < lights.size(), "invalid image-based light index");
        const auto light = lights[*selected].text;
        auto& environment = model.environment.emplace();
        const auto faceSize = index(property(light, "specularImageSize"));
        require(faceSize && std::has_single_bit(*faceSize), "invalid environment face size");
        environment.faceSize = *faceSize;
        const auto mips = gltfJsonArray(property(light, "specularImages").text);
        require(!mips.empty() && mips.size() <= std::bit_width(*faceSize),
                "invalid count of environment mips");
        for (const auto& mip : mips)
        {
            const auto faces = gltfJsonArray(mip.text);
            auto& result = environment.mips.emplace_back();
            require(faces.size() == result.size(), "environment mip needs six faces");
            for (size_t i = 0; i < faces.size(); ++i)
            {
                const auto image = index(faces[i]);
                require(image && *image < data->images_count, "invalid environment face image");
                result[i] = imageSource(&data->images[*image], {});
            }
        }
        const auto coefficients = gltfJsonArray(property(light, "irradianceCoefficients").text);
        require(coefficients.size() == environment.irradiance.size(),
                "environment needs nine irradiance coefficients");
        for (size_t i = 0; i < coefficients.size(); ++i)
            environment.irradiance[i] = jsonVector<3>(coefficients[i]);
        if (const auto value = gltfJsonProperty(light, "intensity"))
        {
            environment.intensity = jsonNumber(*value);
            require(environment.intensity >= 0.0f, "invalid environment intensity");
        }
        if (const auto value = gltfJsonProperty(light, "rotation"))
        {
            const auto rotation = jsonVector<4>(*value);
            const float length = glm::length(rotation);
            require(std::isfinite(length) && length > 0.0f, "invalid environment rotation");
            environment.rotation =
                glm::quat(rotation.w, rotation.x, rotation.y, rotation.z) / length;
        }
    }
    require(data->nodes_count <= UINT32_MAX, "too many scene nodes");
    std::vector<uint32_t> sceneIndices(data->nodes_count, noParentNode);
    std::vector<const cgltf_node*> sceneSources;
    std::vector<std::pair<const cgltf_node*, uint32_t>> nodes;
    for (cgltf_size i = 0; i < scene->nodes_count; ++i)
        nodes.emplace_back(scene->nodes[i], noParentNode);
    model.bounds = {glm::vec3(std::numeric_limits<float>::max()),
                    glm::vec3(std::numeric_limits<float>::lowest())};
    auto& [minimum, maximum] = model.bounds;
    while (!nodes.empty())
    {
        const auto [node, parent] = nodes.back();
        nodes.pop_back();
        auto& nodeIndex = sceneIndices[cgltf_node_index(data.get(), node)];
        require(nodeIndex == noParentNode, "repeated scene node");
        nodeIndex = static_cast<uint32_t>(model.scene.nodes.size());
        auto& sceneNode = model.scene.nodes.emplace_back();
        sceneNode.parent = parent;
        if (const auto* extension =
                findExtension({node->extensions, node->extensions_count}, "KHR_node_visibility"))
            if (const auto value = gltfJsonProperty(extension->data, "visible"))
            {
                require(!value->isString && (value->text == "true" || value->text == "false"),
                        "invalid node visibility");
                sceneNode.visible = value->text == "true";
            }
        model.scene.restVisibility.push_back(
            sceneNode.visible && (parent == noParentNode || model.scene.restVisibility[parent]));
        if (node->has_matrix)
            sceneNode.matrix = glm::make_mat4(node->matrix);
        else
        {
            const auto& q = node->rotation;
            const auto rotation = glm::quat(q[3], q[0], q[1], q[2]);
            const float length = glm::length(rotation);
            require(std::isfinite(length) && length > 0.0f, "invalid node rotation");
            sceneNode.transform = {glm::make_vec3(node->translation), rotation / length,
                                   glm::make_vec3(node->scale)};
        }
        const auto local = sceneNode.matrix ? *sceneNode.matrix : sceneNode.transform.matrix();
        const auto transform =
            parent == noParentNode ? local : model.scene.restWorld[parent] * local;
        require(std::ranges::all_of(std::span(glm::value_ptr(transform), 16),
                                    [](float value)
                                    {
                                        return std::isfinite(value);
                                    }),
                "invalid node transform");
        model.scene.restWorld.push_back(transform);
        if (const auto* light = node->light)
        {
            require(light->type == cgltf_light_type_directional ||
                        light->type == cgltf_light_type_point || light->type == cgltf_light_type_spot,
                    "invalid punctual light type");
            const auto type = light->type == cgltf_light_type_directional
                                  ? PunctualLightType::Directional
                              : light->type == cgltf_light_type_spot ? PunctualLightType::Spot
                                                                   : PunctualLightType::Point;
            require(std::ranges::all_of(light->color, [](float value)
                                        { return value >= 0.0f && value <= 1.0f; }) &&
                        std::isfinite(light->intensity) && light->intensity >= 0.0f &&
                        std::isfinite(light->range) && light->range >= 0.0f,
                    "invalid punctual light parameters");
            const float inner = light->spot_inner_cone_angle, outer = light->spot_outer_cone_angle;
            require(type != PunctualLightType::Spot ||
                        (inner >= 0.0f && inner < outer && outer <= glm::radians(90.0f)),
                    "invalid spot light cone");
            model.scene.lights.push_back({nodeIndex, type, glm::make_vec3(light->color),
                                          light->intensity, light->range,
                                          {std::cos(inner), std::cos(outer)}});
        }
        sceneSources.push_back(node);
        for (cgltf_size i = 0; i < node->children_count; ++i)
            nodes.emplace_back(node->children[i], nodeIndex);
    }

    std::vector<uint32_t> skinOffsets(data->skins_count, noSkin);
    const auto getSkin = [&](const cgltf_skin* skin)
    {
        if (!skin)
            return noSkin;
        auto& offset = skinOffsets[cgltf_skin_index(data.get(), skin)];
        if (offset != noSkin)
            return offset;
        require(skin->joints_count > 0 &&
                    skin->joints_count <= UINT32_MAX - model.scene.joints.size(),
                "invalid skin size");
        offset = static_cast<uint32_t>(model.scene.joints.size());
        const auto* accessor = skin->inverse_bind_matrices;
        require(accessor && accessor->type == cgltf_type_mat4 &&
                    accessor->component_type == cgltf_component_type_r_32f &&
                    accessor->count >= skin->joints_count,
                "invalid or missing inverse bind accessor");
        std::vector<glm::mat4> matrices(accessor->count);
        require(readAccessor(*accessor, accessor->count,
                             [&](size_t i)
                             {
                                 return glm::value_ptr(matrices[i]);
                             }),
                "cannot read inverse bind matrices");
        for (size_t i = 0; i < skin->joints_count; ++i)
        {
            const auto joint = sceneIndices[cgltf_node_index(data.get(), skin->joints[i])];
            require(joint != noParentNode, "skin joint is outside the active scene");
            const auto& inverseBind = matrices[i];
            const auto rest = model.scene.restWorld[joint] * inverseBind;
            require(std::ranges::all_of(std::span(glm::value_ptr(rest), 16),
                                        [](float value)
                                        {
                                            return std::isfinite(value);
                                        }),
                    "invalid inverse bind matrix");
            model.scene.joints.push_back({joint, inverseBind});
            model.scene.restJoints.push_back(rest);
        }
        return offset;
    };

    for (uint32_t nodeIndex = 0; nodeIndex < sceneSources.size(); ++nodeIndex)
    {
        const auto* node = sceneSources[nodeIndex];
        if (!node->mesh)
            continue;
        const bool instanced = node->has_mesh_gpu_instancing;
        const glm::vec3 boundsLimit(std::numeric_limits<float>::max());
        ModelBounds localBounds{boundsLimit, -boundsLimit};
        auto& sceneNode = model.scene.nodes[nodeIndex];
        const auto morphCount =
            node->mesh->primitives_count ? node->mesh->primitives[0].targets_count : 0;
        require(!instanced || (!node->skin && !morphCount),
                "instancing with skinning or morph targets is unsupported");
        const auto skin = getSkin(node->skin);
        std::span<glm::mat4> transforms;
        uint32_t firstInstance = 0, instanceCount = 1;
        if (instanced)
        {
            const auto& attributes = node->mesh_gpu_instancing;
            const auto count = attributes.attributes[0].data->count;
            const auto start = model.instanceTransforms.size();
            require(count > 0 && count <= UINT32_MAX - start, "invalid instance count");
            model.instanceTransforms.resize(start + count);
            transforms = std::span(model.instanceTransforms).subspan(start, count);
            require(readInstanceTransforms(attributes, transforms),
                    "invalid or unsupported instance transforms");
            firstInstance = static_cast<uint32_t>(start + 1);
            instanceCount = static_cast<uint32_t>(count);
        }
        if (morphCount)
        {
            require(skin == noSkin, "combining skinning and morph targets is unsupported");
            const auto* weights = node->weights ? node->weights : node->mesh->weights;
            const auto weightCount =
                node->weights ? node->weights_count : node->mesh->weights_count;
            require((!weights || weightCount == morphCount) &&
                        morphCount <= UINT32_MAX - model.scene.restWeights.size(),
                    "invalid default morph weights");
            sceneNode.morphOffset = static_cast<uint32_t>(model.scene.restWeights.size());
            sceneNode.morphBounds.resize(morphCount, {boundsLimit, -boundsLimit});
            for (size_t target = 0; target < morphCount; ++target)
            {
                const float weight = weights ? weights[target] : 0.0f;
                require(std::isfinite(weight), "invalid default morph weight");
                model.scene.restWeights.push_back(weight);
            }
        }
        const auto appendPrimitive =
            [&](const cgltf_primitive& primitive, const cgltf_material* source)
        {
            if (progress)
                progress(ImportStage::BuildingMeshes);
            const auto materialIndex = getMaterial(source);
            const auto* positions =
                cgltf_find_accessor(&primitive, cgltf_attribute_type_position, 0);
            require(positions, "POSITION is required");
            require(!primitive.indices || !primitive.indices->is_sparse,
                    "sparse index accessors are unsupported");
            const auto sourceCount = primitive.indices ? primitive.indices->count : positions->count;
            require(sourceCount <= UINT32_MAX, "too many source indices");
            const auto [topology, indexCount] = primitiveLayout(primitive.type, sourceCount);
            const bool triangles = topology == PrimitiveTopology::Triangles;
            const auto* normals = cgltf_find_accessor(&primitive, cgltf_attribute_type_normal, 0);
            const auto* colors = cgltf_find_accessor(&primitive, cgltf_attribute_type_color, 0);
            const auto& views = materialViews[materialIndex];
            const auto& material = model.materials[materialIndex];
            require(!instanced || material.alphaMode != AlphaMode::Blend,
                    "alpha blending on instanced meshes is unsupported");
            const auto uvSlots = materialUvSlots[materialIndex];
            const bool animatedNormalUv = (uvSlots & (1u << NormalSlot)) != 0;
            const auto& normalView = views[NormalSlot];
            const auto* tangents =
                normals && (normalView.texture || (!triangles && views[ClearcoatNormalSlot].texture) ||
                            (material.advanced.anisotropy.x > 0 && !views[AnisotropySlot].texture))
                    ? cgltf_find_accessor(&primitive, cgltf_attribute_type_tangent, 0)
                    : nullptr;
            const auto& normalUV = normalView.transform;
            const bool flipsU = !animatedNormalUv && normalUV.scale[0] < 0.0f;
            const bool flipsV = !animatedNormalUv && normalUV.scale[1] < 0.0f;
            const bool reorientsNormalUV = !animatedNormalUv && normalView.has_transform &&
                                           (normalUV.rotation != 0.0f || flipsU || flipsV);
            require(positions->type == cgltf_type_vec3 &&
                        (!normals || normals->type == cgltf_type_vec3) &&
                        (!colors || colors->type == cgltf_type_vec3 ||
                         colors->type == cgltf_type_vec4) &&
                        (!tangents || tangents->type == cgltf_type_vec4),
                    "invalid vertex attribute types");
            require(positions->count > 0 && positions->count <= INT32_MAX - model.vertices.size() &&
                        indexCount > 0 && indexCount <= UINT32_MAX - model.indices.size(),
                    "invalid mesh size");
            PrimitiveData geometry{primitive, {}};
            require(geometry.decode(positions->count, sourceCount),
                    "Draco mesh decode failed or does not match its accessors");
            Draw draw{static_cast<uint32_t>(model.indices.size()),
                      static_cast<uint32_t>(indexCount),
                      static_cast<int32_t>(model.vertices.size()), materialIndex};
            draw.node = nodeIndex;
            draw.skin = skin;
            draw.morphCount = static_cast<uint32_t>(morphCount);
            draw.firstInstance = firstInstance;
            draw.instanceCount = instanceCount;
            draw.topology = topology;
            draw.hasNormals = triangles || normals;
            draw.flatMorphNormals = triangles && morphCount && !normals;
            model.vertices.resize(model.vertices.size() + positions->count);
            model.indices.resize(model.indices.size() + indexCount);
            auto indices = std::span(model.indices).subspan(draw.firstIndex, draw.indexCount);
            const auto sourceIndices = indices.first(sourceCount);
            require(geometry.readIndices(sourceIndices), "index read failed");
            for (const auto index : sourceIndices)
                require(index < positions->count, "index exceeds vertex count");
            expandPrimitiveIndices(primitive.type, sourceCount, indices);
            {
                auto vertices = std::span(model.vertices).subspan(draw.vertexOffset);
                require(
                    readMorphTargets(primitive, vertices, model.morphDeltas, sceneNode.morphBounds),
                    "invalid morph target accessors");
                require(geometry.read(positions, cgltf_attribute_type_position, vertices,
                                      &Vertex::position) &&
                            geometry.read(normals, cgltf_attribute_type_normal, vertices,
                                          &Vertex::normal) &&
                            geometry.read(colors, cgltf_attribute_type_color, vertices,
                                          &Vertex::color) &&
                            geometry.read(tangents, cgltf_attribute_type_tangent, vertices,
                                          &Vertex::tangent),
                        "vertex read failed");
                if (skin != noSkin)
                {
                    for (size_t i = 0; i < primitive.attributes_count; ++i)
                    {
                        const auto& attribute = primitive.attributes[i];
                        require((attribute.type != cgltf_attribute_type_joints &&
                                 attribute.type != cgltf_attribute_type_weights) ||
                                    attribute.index == 0,
                                "more than four skin influences are unsupported");
                    }
                    require(readSkinInfluences(geometry, vertices, node->skin->joints_count),
                            "invalid skin joint indices or weights");
                }
                for (size_t slot = 0; slot < BaseTextureCount; ++slot)
                    if (views[slot].texture)
                        require(readTextureCoordinates(geometry, views[slot], vertices.size(),
                                                       (uvSlots & (1u << slot)) != 0,
                                                       [&](size_t i)
                                                       {
                                                           return glm::value_ptr(
                                                               vertices[i].uv[slot]);
                                                       }),
                                "missing or invalid texture coordinates");
                const bool anisotropyUv =
                    material.advanced.anisotropy.x > 0 && !tangents &&
                    !views[AnisotropySlot].texture &&
                    cgltf_find_accessor(&primitive, cgltf_attribute_type_texcoord, 0);
                if (anisotropyUv || std::ranges::any_of(std::span(views).subspan(BaseTextureCount),
                                                        [](const auto& view)
                                                        {
                                                            return view.texture != nullptr;
                                                        }))
                {
                    const auto start = model.advancedUvs.size();
                    require(vertices.size() <= (UINT32_MAX - start) / advancedUvPairs,
                            "too many material texture coordinates");
                    model.advancedUvs.resize(start + vertices.size() * advancedUvPairs,
                                             glm::vec4(0));
                    for (size_t i = 0; i < vertices.size(); ++i)
                        vertices[i].advancedUv =
                            static_cast<uint32_t>(start + i * advancedUvPairs + 1);
                    for (size_t slot = BaseTextureCount; slot < views.size(); ++slot)
                        if (views[slot].texture || (slot == AnisotropySlot && anisotropyUv))
                        {
                            const auto coord = slot - BaseTextureCount;
                            require(
                                readTextureCoordinates(
                                    geometry, views[slot], vertices.size(),
                                    (uvSlots & (1u << slot)) != 0,
                                    [&](size_t i)
                                    {
                                        return glm::value_ptr(
                                                   model.advancedUvs[start + i * advancedUvPairs +
                                                                     coord / 2]) +
                                               2 * (coord % 2);
                                    }),
                                "missing or invalid advanced material texture coordinates");
                        }
                }
                for (auto& vertex : vertices)
                {
                    if (!triangles && !tangents)
                        vertex.tangent.w = 0.0f;
                    require(std::isfinite(vertex.position.x) && std::isfinite(vertex.position.y) &&
                                std::isfinite(vertex.position.z),
                            "invalid vertex position");
                    if (colors)
                        for (int channel = 0; channel < 4; ++channel)
                        {
                            auto& value = vertex.color[channel];
                            require(std::isfinite(value), "invalid vertex color");
                            value = std::clamp(value, 0.0f, 1.0f);
                        }
                }
            }
            if (triangles && !normals)
                require(generateFlatNormals(model.vertices, draw.vertexOffset, indices,
                                            primitive.indices || geometry.mesh ||
                                                primitive.type != cgltf_primitive_type_triangles),
                        "flat normals exceed the vertex limit");
            const auto vertices = std::span(model.vertices).subspan(draw.vertexOffset);
            if (draw.hasNormals)
            {
                for (auto& vertex : vertices)
                {
                    const float length = glm::length(vertex.normal);
                    require(std::isfinite(length) && length > 0.0f, "invalid vertex normal");
                    vertex.normal /= length;
                }
            }
            const auto degenerateTangent = [](const Vertex& vertex)
            {
                return glm::length(glm::cross(vertex.normal, glm::vec3(vertex.tangent))) <= 1e-6f;
            };
            const bool needsTangents = triangles && normalView.texture &&
                                       (!tangents || reorientsNormalUV ||
                                        std::ranges::any_of(vertices, degenerateTangent));
            draw.deriveTangent = triangles && (animatedNormalUv || (morphCount && needsTangents));
            if (needsTangents)
            {
                if (tangents)
                    for (const auto& vertex : vertices)
                        require(std::abs(vertex.tangent.w) == 1.0f,
                                "invalid vertex tangent handedness");
                if (progress)
                    progress(ImportStage::GeneratingTangents);
                // Retain the authored sign when reorienting UVs, reversing it for a mirror.
                const float handedness =
                    tangents && reorientsNormalUV ? (flipsU == flipsV ? 1.0f : -1.0f) : 0.0f;
                require(generateTangents(model.vertices, draw.vertexOffset, indices, handedness),
                        "MikkTSpace tangent generation failed");
                if (progress)
                    progress(ImportStage::BuildingMeshes);
            }
            for (auto& vertex : std::span(model.vertices).subspan(draw.vertexOffset))
            {
                if (needsTangents ? vertex.tangent.w != 0.0f : tangents != nullptr)
                {
                    auto tangent = glm::vec3(vertex.tangent);
                    tangent -= vertex.normal * glm::dot(vertex.normal, tangent);
                    const float tangentLength = glm::length(tangent);
                    require(std::isfinite(tangentLength) && tangentLength > 0.0f &&
                                (vertex.tangent.w == 1.0f || vertex.tangent.w == -1.0f),
                            "invalid vertex tangent");
                    vertex.tangent = glm::vec4(tangent / tangentLength, vertex.tangent.w);
                }
                if (!instanced)
                {
                    const auto position = restPosition(model, draw, vertex);
                    for (int axis = 0; axis < 3; ++axis)
                        require(std::isfinite(position[axis]), "invalid vertex position");
                    minimum = glm::min(minimum, position);
                    maximum = glm::max(maximum, position);
                }
                localBounds.minimum = glm::min(localBounds.minimum, vertex.position);
                localBounds.maximum = glm::max(localBounds.maximum, vertex.position);
                if (skin != noSkin)
                    forEachInfluence(vertex,
                                     [&](uint16_t joint, float)
                                     {
                                         auto& bounds = model.scene.joints[skin + joint].meshBounds;
                                         if (!bounds)
                                             bounds = ModelBounds{vertex.position, vertex.position};
                                         else
                                         {
                                             bounds->minimum =
                                                 glm::min(bounds->minimum, vertex.position);
                                             bounds->maximum =
                                                 glm::max(bounds->maximum, vertex.position);
                                         }
                                     });
            }
            if (model.materials[materialIndex].alphaMode == AlphaMode::Blend)
            {
                require(skin == noSkin && !morphCount,
                        "alpha blending is unsupported on skinned or morphed meshes");
                glm::vec3 partMinimum(std::numeric_limits<float>::max());
                glm::vec3 partMaximum(std::numeric_limits<float>::lowest());
                for (const auto index : indices)
                {
                    const auto& position = model.vertices[draw.vertexOffset + index].position;
                    partMinimum = glm::min(partMinimum, position);
                    partMaximum = glm::max(partMaximum, position);
                }
                draw.center = (partMinimum + partMaximum) * 0.5f;
            }
            model.draws.push_back(std::move(draw));
        };
        for (const auto& primitive :
             std::span(node->mesh->primitives, node->mesh->primitives_count))
        {
            const auto base = model.draws.size();
            appendPrimitive(primitive, primitive.material);
            std::unordered_map<const cgltf_material*, size_t> alternatives;
            for (const auto& mapping : std::span(primitive.mappings, primitive.mappings_count))
            {
                if (mapping.material == primitive.material)
                    continue;
                const auto variant = static_cast<uint32_t>(mapping.variant);
                auto& exclusions = model.draws[base].variants;
                require(std::ranges::find(exclusions, variant) == exclusions.end(),
                        "duplicate material variant mapping");
                exclusions.push_back(variant);
                const auto [choice, inserted] =
                    alternatives.try_emplace(mapping.material, model.draws.size());
                if (inserted)
                {
                    appendPrimitive(primitive, mapping.material);
                    model.draws.back().defaultMaterial = false;
                }
                model.draws[choice->second].variants.push_back(variant);
            }
        }
        if (instanced && node->mesh->primitives_count)
        {
            ModelBounds instancedBounds{boundsLimit, -boundsLimit};
            for (const auto& transform : transforms)
            {
                includeBounds(instancedBounds, localBounds, transform);
                includeBounds(model.bounds, localBounds, model.scene.restWorld[nodeIndex] * transform);
            }
            sceneNode.meshBounds = instancedBounds;
        }
        else if ((skin == noSkin || morphCount) && node->mesh->primitives_count)
            sceneNode.meshBounds = localBounds;
    }
    require(!model.draws.empty(), "no drawable meshes");
    const auto size = maximum - minimum;
    const float longest = std::max({size.x, size.y, size.z});
    require(std::isfinite(longest) &&
                (longest > 0.0f || data->animations_count != 0 ||
                 std::ranges::any_of(model.draws, [](const Draw& draw)
                 {
                     return draw.topology == PrimitiveTopology::Points;
                 })),
            "empty model bounds");
    model.center = (minimum + maximum) * 0.5f;
    if (!model.instanceTransforms.empty())
        model.radius = glm::length(size) * 0.5f;
    for (size_t i = 0; i < model.draws.size(); ++i)
    {
        const auto& draw = model.draws[i];
        if (draw.firstInstance)
            continue;
        const size_t end =
            i + 1 < model.draws.size() ? model.draws[i + 1].vertexOffset : model.vertices.size();
        for (size_t j = draw.vertexOffset; j < end; ++j)
        {
            const auto position = restPosition(model, draw, model.vertices[j]);
            model.radius = std::max(model.radius, glm::length(position - model.center));
        }
    }
    if (model.radius == 0.0f)
        model.radius = 1.0f; // Animated meshes may start with a zero scale.
    require(std::isfinite(model.radius) && model.radius > 0.0f, "invalid model radius");
    for (size_t i = 0; i < data->animations_count; ++i)
    {
        const auto& animation = data->animations[i];
        AnimationClip clip;
        clip.name = animation.name && animation.name[0] ? animation.name
                                                        : "Animation " + std::to_string(i + 1);
        for (size_t c = 0; c < animation.channels_count; ++c)
        {
            if (progress)
                progress(ImportStage::ReadingAnimation);
            const auto& channel = animation.channels[c];
            using enum AnimationTarget;
            AnimationBinding binding;
            if (const auto pointer = pointerTargets.find(&channel); pointer != pointerTargets.end())
                binding = pointer->second;
            else if (channel.target_node)
            {
                binding.index =
                    static_cast<uint32_t>(cgltf_node_index(data.get(), channel.target_node));
                switch (channel.target_path)
                {
                case cgltf_animation_path_type_translation:
                    binding.target = Translation;
                    break;
                case cgltf_animation_path_type_rotation:
                    binding.target = Rotation;
                    break;
                case cgltf_animation_path_type_scale:
                    binding.target = Scale;
                    break;
                case cgltf_animation_path_type_weights:
                    binding.target = Weights;
                    break;
                default:
                    require(false, "unsupported animation target");
                }
            }
            else
                continue;

            size_t morphCount = 0;
            if (binding.target >= BaseColor)
            {
                const auto found =
                    std::ranges::find(materialSources, &data->materials[binding.index]);
                if (found == materialSources.end())
                    continue;
                binding.index = static_cast<uint32_t>(found - materialSources.begin());
                if (binding.target >= TextureOffset &&
                    !(materialUvSlots[binding.index] & (1u << binding.element)))
                    continue; // This texture is unused by the material's shading model.
            }
            else
            {
                binding.index = sceneIndices[binding.index];
                if (binding.index == noParentNode)
                    continue;
                const auto& node = model.scene.nodes[binding.index];
                require(!node.matrix || binding.target == Weights || binding.target == Visibility,
                        "invalid node animation");
                morphCount = node.morphBounds.size();
            }
            require(channel.sampler && channel.sampler->input && channel.sampler->output,
                    "missing animation sampler");
            auto& track = clip.tracks.emplace_back();
            track.binding = binding;
            require(readAnimationTrack(*channel.sampler, track, morphCount),
                    "invalid animation keyframes");
            clip.duration = std::max(clip.duration, track.times.back());
        }
        if (!clip.tracks.empty())
            model.scene.clips.push_back(std::move(clip));
    }
    return model;
}
