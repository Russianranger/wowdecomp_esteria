#ifndef ENGINE_SOURCE_RETAIL_MOUNTOVERRIDES_H
#define ENGINE_SOURCE_RETAIL_MOUNTOVERRIDES_H

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <utility>

namespace Retail
{
struct MountOverride
{
    std::string product, build, model;
    uint32_t locale = 0;
    std::array<std::string, 3> textures;
    float scale = 1.0f;
};

inline bool ParseMountOverrides(const std::string& input, std::map<uint32_t, MountOverride>& output)
{
    std::map<uint32_t, MountOverride> parsed;
    std::istringstream lines(input);
    std::string line;
    const auto number = [](const std::string& value, uint32_t& result, int base = 10)
    {
        const auto conversion = std::from_chars(value.data(), value.data() + value.size(), result, base);
        return !value.empty() && conversion.ec == std::errc() && conversion.ptr == value.data() + value.size();
    };
    while (std::getline(lines, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line.front() == '#')
            continue;
        std::array<std::string, 9> fields;
        std::istringstream columns(line);
        for (auto& field : fields)
            if (!std::getline(columns, field, ';'))
                return false;
        if (columns.peek() != std::char_traits<char>::eof())
            return false;
        MountOverride entry;
        uint32_t display = 0, model = 0;
        if (!number(fields[0], display) || !display || fields[1] != "wow" || fields[2].size() != 32
            || fields[2].find_first_not_of("0123456789abcdef") != std::string::npos
            || !number(fields[3], entry.locale, 16) || !entry.locale || !number(fields[4], model) || !model)
            return false;
        entry.product = fields[1];
        entry.build = fields[2];
        entry.model = "retail\\" + std::to_string(model) + ".m2";
        for (size_t i = 0; i < entry.textures.size(); ++i)
        {
            uint32_t texture = 0;
            if (!number(fields[5 + i], texture))
                return false;
            if (texture)
                entry.textures[i] = std::to_string(texture) + ".blp";
        }
        const auto scale = std::from_chars(fields[8].data(), fields[8].data() + fields[8].size(), entry.scale);
        if (scale.ec != std::errc() || scale.ptr != fields[8].data() + fields[8].size()
            || !std::isfinite(entry.scale) || entry.scale <= 0 || entry.scale > 100
            || !parsed.emplace(display, std::move(entry)).second)
            return false;
    }
    output.swap(parsed);
    return true;
}
}
#endif
