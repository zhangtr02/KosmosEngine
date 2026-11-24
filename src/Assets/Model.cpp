#include "Model.h"

#include <algorithm>
#include <cctype>
#include <cgltf.h>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <memory>
#include <mikktspace.h>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
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
    if (comma == std::string_view::npos)
        return {};
    auto payload = uri.substr(comma + 1);
    if (!uri.substr(0, comma).ends_with(";base64"))
    {
        auto bytes = std::make_shared<std::string>(payload);
        bytes->resize(cgltf_decode_uri(bytes->data()));
        return {
            std::shared_ptr<const uint8_t>(bytes, reinterpret_cast<const uint8_t*>(bytes->data())),
            bytes->size()};
    }

    std::string unescaped;
    if (payload.find('%') != std::string_view::npos)
    {
        unescaped = payload;
        unescaped.resize(cgltf_decode_uri(unescaped.data()));
        payload = unescaped;
    }
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

cgltf_result validateModel(cgltf_data& data)
{
    // cgltf checks the base index values before sparse replacements have been applied.
    std::vector<std::pair<cgltf_accessor*, cgltf_buffer_view*>> sparseBases;
    for (size_t m = 0; m < data.meshes_count; ++m)
    {
        const auto& mesh = data.meshes[m];
        for (size_t p = 0; p < mesh.primitives_count; ++p)
        {
            auto* indices = mesh.primitives[p].indices;
            if (!indices || !indices->is_sparse || !indices->buffer_view)
                continue;
            auto* view = indices->buffer_view;
            const auto size = cgltf_component_size(indices->component_type);
            if (!size || indices->offset > view->size ||
                indices->count > (view->size - indices->offset) / size)
                return cgltf_result_data_too_short;
            sparseBases.emplace_back(indices, view);
        }
    }
    for (const auto& [indices, view] : sparseBases)
        indices->buffer_view = nullptr;
    const auto result = cgltf_validate(&data);
    for (const auto& [indices, view] : sparseBases)
        indices->buffer_view = view;
    return result;
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

bool readIndices(const cgltf_accessor* accessor, std::span<uint32_t> indices)
{
    if (!accessor)
    {
        std::iota(indices.begin(), indices.end(), 0u);
        return true;
    }
    auto base = *accessor;
    base.is_sparse = false;
    if (!base.buffer_view)
        std::ranges::fill(indices, 0u);
    else if (cgltf_accessor_unpack_indices(&base, indices.data(), sizeof(uint32_t),
                                           indices.size()) != indices.size())
        return false;

    return applySparse(*accessor,
                       [&](const cgltf_accessor& values, size_t row, size_t target)
                       {
                           indices[target] =
                               static_cast<uint32_t>(cgltf_accessor_read_index(&values, row));
                           return true;
                       });
}

template <typename ComponentPointer>
bool readAttribute(const cgltf_accessor* accessor, size_t count, ComponentPointer output)
{
    if (!accessor)
        return true;
    auto base = *accessor;
    base.is_sparse = false;
    const auto components = cgltf_num_components(accessor->type);
    const auto read = [&](const cgltf_accessor& values, size_t row, size_t target)
    {
        return cgltf_accessor_read_float(&values, row, output(target), components) != 0;
    };
    for (size_t i = 0; i < count; ++i)
        if (!read(base, i, i))
            return false;
    return applySparse(*accessor, read);
}

template <typename Vector>
bool readAttribute(const cgltf_accessor* accessor, std::span<Vertex> vertices,
                   Vector Vertex::* member)
{
    return readAttribute(accessor, vertices.size(),
                         [&](size_t i)
                         {
                             return glm::value_ptr(vertices[i].*member);
                         });
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
    case cgltf_filter_type_nearest:
    case cgltf_filter_type_linear:
        sampler.mip = MipFilter::None;
        break;
    case cgltf_filter_type_nearest_mipmap_nearest:
    case cgltf_filter_type_linear_mipmap_nearest:
        sampler.mip = MipFilter::Nearest;
        break;
    default:
        break;
    }
    sampler.nearestMin = source->min_filter == cgltf_filter_type_nearest ||
                         source->min_filter == cgltf_filter_type_nearest_mipmap_nearest ||
                         source->min_filter == cgltf_filter_type_nearest_mipmap_linear;
    return sampler;
}

bool readTextureCoordinates(const cgltf_primitive& primitive, const cgltf_texture_view& view,
                            std::span<Vertex> vertices, size_t slot)
{
    if (!view.texture)
        return true;
    const auto& transform = view.transform;
    const int uvSet =
        view.has_transform && transform.has_texcoord ? transform.texcoord : view.texcoord;
    const auto* accessor =
        uvSet >= 0 ? cgltf_find_accessor(&primitive, cgltf_attribute_type_texcoord, uvSet)
                   : nullptr;
    if (!accessor || accessor->type != cgltf_type_vec2 ||
        !readAttribute(accessor, vertices.size(),
                       [&](size_t i)
                       {
                           return glm::value_ptr(vertices[i].uv[slot]);
                       }))
        return false;
    if (view.has_transform)
    {
        const float c = std::cos(transform.rotation), s = std::sin(transform.rotation);
        // Positive rotation is counterclockwise with V pointing down.
        const glm::vec2 u(c * transform.scale[0], -s * transform.scale[0]);
        const glm::vec2 v(s * transform.scale[1], c * transform.scale[1]);
        const auto offset = glm::make_vec2(transform.offset);
        for (auto& vertex : vertices)
        {
            auto& point = vertex.uv[slot];
            point = offset + u * point.x + v * point.y;
        }
    }
    return std::ranges::all_of(vertices,
                               [slot](const Vertex& vertex)
                               {
                                   return std::isfinite(vertex.uv[slot].x) &&
                                          std::isfinite(vertex.uv[slot].y);
                               });
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
    require(validateModel(*data) == cgltf_result_success, "invalid glTF data");
    if (progress)
        progress(ImportStage::BuildingMeshes);
    require(data->animations_count == 0, "animations are unsupported");
    for (cgltf_size i = 0; i < data->extensions_required_count; ++i)
    {
        const std::string_view extension = data->extensions_required[i];
        if (extension != "KHR_materials_emissive_strength" &&
            extension != "KHR_texture_transform" && extension != "KHR_materials_unlit")
            throw std::runtime_error(std::string("Unsupported required glTF extension: ") +
                                     data->extensions_required[i]);
    }

    Model model;
    std::vector<EncodedImage> embeddedImages(data->images_count);
    const auto getTexture = [&](const cgltf_texture_view& view, bool srgb, bool flatNormal = false)
    {
        TextureSource source{.srgb = srgb, .flatNormal = flatNormal};
        if (view.texture)
        {
            const auto* image = view.texture->image;
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
        }
        const auto found = std::find(model.textures.begin(), model.textures.end(), source);
        const auto index = static_cast<uint32_t>(found - model.textures.begin());
        if (found == model.textures.end())
            model.textures.push_back(source);
        return index;
    };
    std::vector<const cgltf_material*> materialSources;
    std::vector<std::array<cgltf_texture_view, MaterialTextureCount>> materialViews;
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
        for (size_t slot = 0; slot < views.size(); ++slot)
        {
            const auto& view = views[slot];
            const bool srgb = slot == BaseColorSlot || slot == EmissiveSlot;
            material.textures[slot] = {getTexture(view, srgb, slot == NormalSlot),
                                       readSampler(view.texture ? view.texture->sampler : nullptr)};
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
        const glm::mat3 linear(transform);
        const float determinant = glm::determinant(linear);
        require(std::isfinite(determinant) && determinant != 0.0f, "singular node transform");
        const auto normalTransform = glm::transpose(glm::inverse(linear));
        for (cgltf_size p = 0; p < node->mesh->primitives_count; ++p)
        {
            if (progress)
                progress(ImportStage::BuildingMeshes);
            const auto& primitive = node->mesh->primitives[p];
            require(primitive.type == cgltf_primitive_type_triangles &&
                        primitive.targets_count == 0 && !primitive.has_draco_mesh_compression,
                    "an uncompressed triangle mesh is required");
            const auto materialIndex = getMaterial(primitive.material);
            const auto* positions =
                cgltf_find_accessor(&primitive, cgltf_attribute_type_position, 0);
            const auto* normals = cgltf_find_accessor(&primitive, cgltf_attribute_type_normal, 0);
            const auto* colors = cgltf_find_accessor(&primitive, cgltf_attribute_type_color, 0);
            const auto& views = materialViews[materialIndex];
            const auto* tangents =
                normals && !model.materials[materialIndex].unlit
                    ? cgltf_find_accessor(&primitive, cgltf_attribute_type_tangent, 0)
                    : nullptr;
            const auto& normalView = views[NormalSlot];
            const auto& normalUV = normalView.transform;
            const bool flipsU = normalUV.scale[0] < 0.0f, flipsV = normalUV.scale[1] < 0.0f;
            const bool reorientsNormalUV =
                normalView.has_transform && (normalUV.rotation != 0.0f || flipsU || flipsV);
            const bool needsTangents = normalView.texture && (!tangents || reorientsNormalUV);
            require(positions, "POSITION is required");
            require(positions->type == cgltf_type_vec3 &&
                        (!normals || normals->type == cgltf_type_vec3) &&
                        (!colors || colors->type == cgltf_type_vec3 ||
                         colors->type == cgltf_type_vec4) &&
                        (!tangents || tangents->type == cgltf_type_vec4),
                    "invalid vertex attribute types");
            const auto indexCount = primitive.indices ? primitive.indices->count : positions->count;
            require(positions->count > 0 && positions->count <= INT32_MAX - model.vertices.size() &&
                        indexCount > 0 && indexCount <= UINT32_MAX - model.indices.size() &&
                        indexCount % 3 == 0,
                    "invalid mesh size");
            Draw draw{static_cast<uint32_t>(model.indices.size()),
                      static_cast<uint32_t>(indexCount),
                      static_cast<int32_t>(model.vertices.size()), materialIndex};
            model.vertices.resize(model.vertices.size() + positions->count);
            model.indices.resize(model.indices.size() + indexCount);
            auto indices = std::span(model.indices).subspan(draw.firstIndex, draw.indexCount);
            require(readIndices(primitive.indices, indices), "index read failed");
            for (const auto index : indices)
                require(index < positions->count, "index exceeds vertex count");
            {
                auto vertices = std::span(model.vertices).subspan(draw.vertexOffset);
                require(readAttribute(positions, vertices, &Vertex::position) &&
                            readAttribute(normals, vertices, &Vertex::normal) &&
                            readAttribute(colors, vertices, &Vertex::color) &&
                            readAttribute(tangents, vertices, &Vertex::tangent),
                        "vertex read failed");
                for (size_t slot = 0; slot < views.size(); ++slot)
                    require(readTextureCoordinates(primitive, views[slot], vertices, slot),
                            "missing or invalid texture coordinates");
                for (auto& vertex : vertices)
                {
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
            if (!normals)
                require(generateFlatNormals(model.vertices, draw.vertexOffset, indices,
                                            primitive.indices != nullptr),
                        "flat normals exceed the vertex limit");
            if (needsTangents)
            {
                for (auto& vertex : std::span(model.vertices).subspan(draw.vertexOffset))
                {
                    const float length = glm::length(vertex.normal);
                    require(std::isfinite(length) && length > 0.0f, "invalid vertex normal");
                    vertex.normal /= length;
                    require(!tangents || std::abs(vertex.tangent.w) == 1.0f,
                            "invalid vertex tangent handedness");
                }
                if (progress)
                    progress(ImportStage::GeneratingTangents);
                // Rebuild the direction from the transformed UVs and retain authored handedness,
                // reversing it when the UV transform mirrors the texture.
                const float handedness = tangents ? (flipsU == flipsV ? 1.0f : -1.0f) : 0.0f;
                require(generateTangents(model.vertices, draw.vertexOffset, indices, handedness),
                        "MikkTSpace tangent generation failed");
                if (progress)
                    progress(ImportStage::BuildingMeshes);
            }
            // Node transforms are baked into vertices; preserve CCW fronts after mirroring.
            if (determinant < 0.0f)
                for (size_t i = 0; i < indices.size(); i += 3)
                    std::swap(indices[i + 1], indices[i + 2]);
            for (auto& vertex : std::span(model.vertices).subspan(draw.vertexOffset))
            {
                vertex.position = glm::vec3(transform * glm::vec4(vertex.position, 1.0f));
                vertex.normal = normalTransform * vertex.normal;
                const float length = glm::length(vertex.normal);
                require(std::isfinite(length) && length > 0.0f, "invalid vertex normal");
                vertex.normal /= length;
                if (needsTangents ? vertex.tangent.w != 0.0f : tangents != nullptr)
                {
                    auto tangent = linear * glm::vec3(vertex.tangent);
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
                minimum = glm::min(minimum, vertex.position);
                maximum = glm::max(maximum, vertex.position);
            }
            glm::vec3 partMinimum(std::numeric_limits<float>::max());
            glm::vec3 partMaximum(std::numeric_limits<float>::lowest());
            for (const auto index : indices)
            {
                const auto& position = model.vertices[draw.vertexOffset + index].position;
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
    model.bounds = {minimum, maximum};
    model.center = (minimum + maximum) * 0.5f;
    for (const auto& vertex : model.vertices)
        model.radius = std::max(model.radius, glm::length(vertex.position - model.center));
    require(std::isfinite(model.radius) && model.radius > 0.0f, "invalid model radius");
    return model;
}
