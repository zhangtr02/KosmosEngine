#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include "cgltf_json.h"
#include <string>
#include <vector>

namespace
{
std::vector<jsmntok_t> tokenize(std::string_view json, jsmntype_t root)
{
    // Reuse the tokenizer in the cgltf implementation for extension objects.
    jsmn_parser parser;
    jsmn_init(&parser);
    const int count = jsmn_parse(&parser, json.data(), json.size(), nullptr, 0);
    if (count <= 0)
        return {};
    std::vector<jsmntok_t> tokens(count);
    jsmn_init(&parser);
    if (jsmn_parse(&parser, json.data(), json.size(), tokens.data(), tokens.size()) != count ||
        tokens[0].type != root)
        return {};
    return tokens;
}

GltfJsonValue tokenValue(std::string_view json, const jsmntok_t& token)
{
    return {json.substr(token.start, token.end - token.start), token.type == JSMN_STRING};
}
} // namespace

std::optional<GltfJsonValue> gltfJsonProperty(std::string_view json, std::string_view key)
{
    const auto tokens = tokenize(json, JSMN_OBJECT);
    if (tokens.empty())
        return std::nullopt;
    const int count = static_cast<int>(tokens.size());
    for (int i = 1; i > 0 && i + 1 < count; i = cgltf_skip_json(tokens.data(), i + 1))
    {
        const auto& name = tokens[i];
        std::string decoded(json.substr(name.start, name.end - name.start));
        decoded.resize(cgltf_decode_string(decoded.data()));
        if (decoded == key)
            return tokenValue(json, tokens[i + 1]);
    }
    return std::nullopt;
}

std::vector<GltfJsonValue> gltfJsonArray(std::string_view json)
{
    const auto tokens = tokenize(json, JSMN_ARRAY);
    std::vector<GltfJsonValue> values;
    if (tokens.empty())
        return values;
    const int count = static_cast<int>(tokens.size());
    values.reserve(tokens[0].size);
    for (int i = 1; i > 0 && i < count; i = cgltf_skip_json(tokens.data(), i))
        values.push_back(tokenValue(json, tokens[i]));
    return values;
}

bool gltfTextureView(std::string_view json, const cgltf_data& data, cgltf_texture_view& view)
{
    const auto tokens = tokenize(json, JSMN_OBJECT);
    if (tokens.empty())
        return false;
    cgltf_options options{};
    if (cgltf_parse_json_texture_view(&options, tokens.data(), 0,
                                      reinterpret_cast<const uint8_t*>(json.data()), &view) < 0)
        return false;
    const auto index = reinterpret_cast<cgltf_size>(view.texture);
    view.texture = nullptr;
    if (!index || index > data.textures_count)
        return false;
    view.texture = &data.textures[index - 1];
    return true;
}
