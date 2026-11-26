#include "FbxImporter.h"
#include "Tangents.h"
#include "TextureImporter.h"

#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <ufbx.h>
#include <unordered_map>

namespace
{
std::string_view text(ufbx_string value) { return {value.data, value.length}; }

glm::vec3 vec3(ufbx_vec3 value) { return {float(value.x), float(value.y), float(value.z)}; }

glm::mat4 matrix(const ufbx_matrix& source)
{
    glm::mat4 result(1);
    for (int column = 0; column < 4; ++column)
        result[column] = glm::vec4(vec3(source.cols[column]), column == 3 ? 1.0f : 0.0f);
    return result;
}

ModelBounds emptyBounds()
{
    const glm::vec3 limit(std::numeric_limits<float>::max());
    return {limit, -limit};
}

std::filesystem::path texturePath(const ufbx_texture& texture)
{
    if (texture.type != UFBX_TEXTURE_FILE || !texture.relative_filename.length)
        throw std::runtime_error("FBX textures must reference local files");
    auto relative = std::string(text(texture.relative_filename));
    std::ranges::replace(relative, '\\', '/');
    return utf8Path(relative).lexically_normal();
}

using TextureSlots = std::array<const ufbx_texture*, BaseTextureCount>;

struct FbxMaterial
{
    uint32_t index{};
    TextureSlots textures{};
};

FbxMaterial addMaterial(Model& model, const ufbx_material* source)
{
    FbxMaterial result{static_cast<uint32_t>(model.materials.size())};
    Material material;
    material.metallicFactor = 0.0f;
    if (source)
    {
        const auto& maps = source->fbx;
        result.textures[BaseColorSlot] = maps.diffuse_color.texture;
        result.textures[NormalSlot] = maps.normal_map.texture;
        result.textures[EmissiveSlot] = maps.emission_color.texture;

        const auto* specular = maps.specular_color.texture;
        // Bistro packs occlusion, roughness and metalness in its *_Specular.dds maps.
        if (specular && specular->relative_filename.length &&
            text(specular->relative_filename).ends_with("_Specular.dds"))
        {
            result.textures[MetallicRoughnessSlot] = result.textures[OcclusionSlot] = specular;
            material.metallicFactor = 1.0f;
        }
        const auto* opacity = ufbx_find_prop(&source->props, "Opacity");
        material.baseColorFactor.a =
            opacity ? std::clamp(float(opacity->value_real), 0.0f, 1.0f) : 1.0f;
        if (!result.textures[BaseColorSlot])
            material.baseColorFactor =
                glm::vec4(vec3(maps.diffuse_color.value_vec3), material.baseColorFactor.a);
        if (result.textures[EmissiveSlot])
            material.emissiveFactor = glm::vec3(1);
        else
            material.emissiveFactor =
                vec3(maps.emission_color.value_vec3) * float(maps.emission_factor.value_real);
        material.normalScale = result.textures[NormalSlot] ? 1.0f : 0.0f;
        material.doubleSided = source->features.double_sided.enabled;
    }

    for (size_t slot = 0; slot < MaterialTextureCount; ++slot)
    {
        TextureSource image;
        image.srgb = slot == BaseColorSlot || slot == EmissiveSlot || slot == SpecularColorSlot ||
                     slot == SheenColorSlot || slot == DiffuseTransmissionColorSlot;
        image.flatNormal = slot == NormalSlot || slot == ClearcoatNormalSlot;
        if (slot < result.textures.size() && result.textures[slot])
        {
            image.path = texturePath(*result.textures[slot]);
            if (fileExtension(image.path) == ".dds")
                image.encoding = TextureEncoding::Dds;
        }
        const auto found = std::ranges::find(model.textures, image);
        material.textures[slot].image = static_cast<uint32_t>(found - model.textures.begin());
        if (found == model.textures.end())
            model.textures.push_back(std::move(image));
    }
    if (material.baseColorFactor.a < 1.0f)
    {
        material.alphaMode = AlphaMode::Blend;
        material.doubleSided = true;
    }
    model.materials.push_back(material);
    model.scene.restMaterials.push_back(material);
    return result;
}

void appendMeshPart(Model& model, const ufbx_mesh& mesh, const ufbx_mesh_part& part,
                    const FbxMaterial& material, uint32_t node, ModelBounds& meshBounds,
                    const ImportProgress& progress)
{
    if (part.num_triangles > (UINT32_MAX - model.indices.size()) / 3 ||
        part.num_triangles > (INT32_MAX - model.vertices.size()) / 3)
        throw std::runtime_error("FBX mesh exceeds the vertex or index limit");

    // Resolve each texture's FBX UV set once per part.
    std::array<const ufbx_vertex_vec2*, BaseTextureCount> uvSets;
    uvSets.fill(&mesh.vertex_uv);
    for (size_t slot = 0; slot < uvSets.size(); ++slot)
        if (const auto* texture = material.textures[slot]; texture && texture->uv_set.length)
            for (const auto& set : mesh.uv_sets)
                if (text(set.name) == text(texture->uv_set))
                {
                    uvSets[slot] = &set.vertex_uv;
                    break;
                }

    std::vector<Vertex> vertices;
    vertices.reserve(part.num_triangles * 3);
    std::vector<uint32_t> triangles(mesh.max_face_triangles * 3);
    auto bounds = emptyBounds();
    for (uint32_t faceIndex : part.face_indices)
    {
        const auto face = mesh.faces.data[faceIndex];
        if (face.num_indices < 3)
            continue;
        const auto count = ufbx_triangulate_face(triangles.data(), triangles.size(), &mesh, face);
        if (count != face.num_indices - 2)
            throw std::runtime_error("Cannot triangulate FBX face");
        for (uint32_t corner : std::span(triangles).first(size_t(count) * 3))
        {
            Vertex vertex{};
            vertex.position = vec3(ufbx_get_vertex_vec3(&mesh.vertex_position, corner));
            const auto normal = vec3(ufbx_get_vertex_vec3(&mesh.vertex_normal, corner));
            const float length = glm::length(normal);
            vertex.normal = length > 0.0f ? normal / length : glm::vec3(0, 1, 0);
            for (size_t slot = 0; slot < uvSets.size(); ++slot)
                if (uvSets[slot]->exists)
                {
                    const auto uv = ufbx_get_vertex_vec2(uvSets[slot], corner);
                    vertex.uv[slot] = {float(uv.x), 1.0f - float(uv.y)};
                }
            bounds.minimum = glm::min(bounds.minimum, vertex.position);
            bounds.maximum = glm::max(bounds.maximum, vertex.position);
            vertices.push_back(vertex);
        }
    }
    if (vertices.empty())
        return;

    Draw draw{static_cast<uint32_t>(model.indices.size()), static_cast<uint32_t>(vertices.size()),
              static_cast<int32_t>(model.vertices.size()), material.index};
    draw.node = node;
    draw.center = (bounds.minimum + bounds.maximum) * 0.5f;
    model.indices.resize(model.indices.size() + vertices.size());
    auto indices = std::span(model.indices).subspan(draw.firstIndex, draw.indexCount);
    const ufbx_vertex_stream stream{vertices.data(), vertices.size(), sizeof(Vertex)};
    const auto size =
        ufbx_generate_indices(&stream, 1, indices.data(), indices.size(), nullptr, nullptr);
    if (!size)
        throw std::runtime_error("Cannot index FBX mesh");
    model.vertices.insert(model.vertices.end(), vertices.begin(), vertices.begin() + size);
    if (material.textures[NormalSlot])
    {
        if (progress)
            progress(ImportStage::GeneratingTangents);
        if (!generateTangents(model.vertices, draw.vertexOffset, indices))
            throw std::runtime_error("Cannot generate FBX tangents");
    }
    meshBounds.minimum = glm::min(meshBounds.minimum, bounds.minimum);
    meshBounds.maximum = glm::max(meshBounds.maximum, bounds.maximum);
    model.draws.push_back(std::move(draw));
}
} // namespace

ImportedModel importFbx(const std::filesystem::path& source, const std::filesystem::path& directory,
                        const ImportProgress& progress)
{
    if (progress)
        progress(ImportStage::Reading);
    ufbx_load_opts options{};
    options.file_format = UFBX_FILE_FORMAT_FBX;
    options.target_axes = ufbx_axes_right_handed_y_up;
    options.target_unit_meters = 1.0;
    options.generate_missing_normals = true;
    options.ignore_animation = true;
    options.ignore_embedded = true;
    struct ReadProgress
    {
        const ImportProgress& report;
        std::exception_ptr error;
    } read{progress, {}};
    if (progress)
    {
        options.progress_cb.user = &read;
        options.progress_cb.fn = [](void* user, const ufbx_progress*)
        {
            auto& state = *static_cast<ReadProgress*>(user);
            try
            {
                state.report(ImportStage::Reading);
                return UFBX_PROGRESS_CONTINUE;
            }
            catch (...)
            {
                state.error = std::current_exception();
                return UFBX_PROGRESS_CANCEL;
            }
        };
    }

    ufbx_error error{};
    const auto filename = pathUtf8(source);
    const std::unique_ptr<ufbx_scene, decltype(&ufbx_free_scene)> scene(
        ufbx_load_file(filename.c_str(), &options, &error), ufbx_free_scene);
    if (read.error)
        std::rethrow_exception(read.error);
    if (!scene)
    {
        char message[512];
        ufbx_format_error(message, sizeof(message), &error);
        throw std::runtime_error("Cannot load FBX: " + filename + " (" + message + ")");
    }

    Model model;
    model.bounds = emptyBounds();
    std::unordered_map<const ufbx_material*, FbxMaterial> materials;
    for (const auto* node : scene->nodes)
    {
        if (!node->mesh)
            continue;
        if (progress)
            progress(ImportStage::BuildingMeshes);
        auto bounds = emptyBounds();
        const auto nodeIndex = static_cast<uint32_t>(model.scene.nodes.size());
        const auto drawStart = model.draws.size();
        for (const auto& part : node->mesh->material_parts)
        {
            if (!part.num_triangles)
                continue;
            const auto* sourceMaterial =
                part.index < node->materials.count ? node->materials.data[part.index] : nullptr;
            const auto [material, inserted] = materials.try_emplace(sourceMaterial);
            if (inserted)
                material->second = addMaterial(model, sourceMaterial);
            appendMeshPart(model, *node->mesh, part, material->second, nodeIndex, bounds, progress);
        }
        if (model.draws.size() == drawStart)
            continue;

        // The evaluated geometry transform includes pivots and the inherited node transform.
        const auto world = matrix(node->geometry_to_world);
        auto& modelNode = model.scene.nodes.emplace_back();
        modelNode.matrix = world;
        modelNode.meshBounds = bounds;
        modelNode.visible = node->visible;
        model.scene.restWorld.push_back(world);
        model.scene.restVisibility.push_back(node->visible);
        includeBounds(model.bounds, bounds, world);
    }
    model.center = (model.bounds.minimum + model.bounds.maximum) * 0.5f;
    model.radius = glm::length(model.bounds.maximum - model.bounds.minimum) * 0.5f;
    if (model.draws.empty() || !std::isfinite(model.radius) || model.radius <= 0.0f)
        throw std::runtime_error("FBX has no drawable mesh bounds");

    std::vector<std::filesystem::path> dependencies;
    for (const auto& texture : model.textures)
        if (!texture.path.empty())
            dependencies.push_back(texture.path);
    StagedModel staged(source, directory, dependencies, progress);
    for (auto& texture : model.textures)
        if (!texture.path.empty())
            texture.path = staged.file().parent_path() / texture.path;
    std::vector<std::optional<bool>> cutouts(model.textures.size());
    for (auto& material : model.materials)
    {
        const auto image = material.textures[BaseColorSlot].image;
        if (material.alphaMode != AlphaMode::Opaque ||
            model.textures[image].encoding != TextureEncoding::Dds)
            continue;
        auto& cutout = cutouts[image];
        if (!cutout)
        {
            if (progress)
                progress(ImportStage::Reading);
            cutout = hasDdsCutout(model.textures[image]);
        }
        if (*cutout)
        {
            material.alphaMode = AlphaMode::Mask;
            material.doubleSided = true;
        }
    }
    return staged.finish(std::move(model));
}
