#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include "cgltf_json.h"
#include <string>
#include <vector>

std::optional<GltfJsonValue> gltfJsonProperty(std::string_view json, std::string_view key)
{
    // Reuse the tokenizer in the cgltf implementation for extension objects.
    jsmn_parser parser;
    jsmn_init(&parser);
    const int count = jsmn_parse(&parser, json.data(), json.size(), nullptr, 0);
    if (count <= 0)
        return std::nullopt;
    std::vector<jsmntok_t> tokens(count);
    jsmn_init(&parser);
    if (jsmn_parse(&parser, json.data(), json.size(), tokens.data(), tokens.size()) != count ||
        tokens[0].type != JSMN_OBJECT)
        return std::nullopt;
    for (int i = 1; i > 0 && i + 1 < count; i = cgltf_skip_json(tokens.data(), i + 1))
    {
        const auto& name = tokens[i];
        std::string decoded(json.substr(name.start, name.end - name.start));
        decoded.resize(cgltf_decode_string(decoded.data()));
        if (decoded == key)
        {
            const auto& value = tokens[i + 1];
            return GltfJsonValue{json.substr(value.start, value.end - value.start),
                                 value.type == JSMN_STRING};
        }
    }
    return std::nullopt;
}
