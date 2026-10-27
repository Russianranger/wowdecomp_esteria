#ifndef WOW_SOURCE_UI_OPTIONCVARS_H
#define WOW_SOURCE_UI_OPTIONCVARS_H

#include <cstdint>
#include <cstdlib>

#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"

// Not in the original binary. Small helpers for the Esteria gameplay options (unit outline, loot beam,
// performance overlay): each option is a saved video cvar the options page edits and the renderer reads
// every frame.
namespace OptionCVars {

// Flag 1 keeps the value in Config.wtf with the other video options.
inline CVar* Register(const char* name, const char* help, const char* defaultValue) {
    return CVar::Register(name, help, 1, defaultValue, nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
}

inline float Float(const CVar* cvar, float low, float high, float fallback) {
    if (!cvar) {
        return fallback;
    }

    float value = cvar->m_floatValue;
    return value < low ? low : (value > high ? high : value);
}

inline int32_t Int(const CVar* cvar, int32_t low, int32_t high, int32_t fallback) {
    if (!cvar) {
        return fallback;
    }

    int32_t value = cvar->m_intValue;
    return value < low ? low : (value > high ? high : value);
}

inline bool Bool(const CVar* cvar, bool fallback) {
    return cvar ? cvar->m_intValue != 0 : fallback;
}

// A colour option is six hex digits, "RRGGBB". Anything else leaves rgb as it was.
inline void Color(const CVar* cvar, float rgb[3]) {
    const char* text = cvar ? cvar->m_stringValue.m_str : nullptr;

    if (!text) {
        return;
    }

    if (text[0] == '#') {
        text++;
    }

    char* end = nullptr;
    unsigned long value = strtoul(text, &end, 16);

    if (end != text + 6 || *end != '\0') {
        return;
    }

    rgb[0] = static_cast<float>((value >> 16) & 0xFF) / 255.0f;
    rgb[1] = static_cast<float>((value >> 8) & 0xFF) / 255.0f;
    rgb[2] = static_cast<float>(value & 0xFF) / 255.0f;
}

}  // namespace OptionCVars

#endif
