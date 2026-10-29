// Not in the original 3.3.5a binary. See OptionsPages.h.
#include "Engine/Source/Lumenveil/OptionsPages.h"

#include "Engine/Source/Lumenveil/Core/config.h"
#include "Engine/Source/Lumenveil/Core/log.h"

#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
    struct Meta {
        const char* key;
        const char* section;
        const char* label;
        const char* format;
        const char* help;
        const char* names;   // a choice's names, separated by '|'
    };

    const Meta kMeta[] = {
#include "Engine/Source/Lumenveil/OptionsMeta.inc"
    };

    // The settings window's sections, gathered into the pages of the Video options.
    struct PageSpec {
        const char* title;
        const char* name;
        const char* subtitle;
        const char* sections[6];
    };

    const PageSpec kPages[] = {
        { "Lumenveil Fog", "LumenveilFogPanel", "Fog and haze: quality, density, drifting banks and mist, open land.",
          { "Quality", "Density", "Drift and mist", "Open land", nullptr } },
        { "Lumenveil Light", "LumenveilLightPanel", "Sun and moon light, shafts, the night, the belt lantern.",
          { "Light", "Sun and moon shafts", "Dusk and night", "Belt lantern", nullptr } },
        { "Lumenveil Colour", "LumenveilColourPanel", "Saturation, white balance, brightness and the sky haze.", { "Colour", nullptr } },
        { "Lumenveil Weather", "LumenveilWeatherPanel", "Weather, wind, clouds and the world's own light.", { "Weather", "World", nullptr } },
        { "Lumenveil Water", "LumenveilWaterPanel", "Water, wet and shiny surfaces, the ground's relief.", { "Water", "Wet and shiny surfaces", "Ground relief", nullptr } },
    };
    constexpr const char* kAdvancedSections[] = { "Debug" };

    bool s_syncing = false;
    std::vector<CVar*> s_cvars;   // by live setting index

    std::string CVarName(const char* key) {
        return std::string("lv") + key;
    }

    // A cvar changed: the running config takes the value. Nothing is written to the ini until "lumenveilSave".
    bool SettingChanged(CVar*, const char*, const char* value, void* arg) {
        if (s_syncing || !value) {
            return true;
        }
        const int index = static_cast<int>(reinterpret_cast<intptr_t>(arg));
        Config edited = GlobalConfig().Global();
        SetLiveSettingValue(edited, index, std::strtof(value, nullptr));
        GlobalConfig().Apply(edited);
        return true;
    }

    void SyncFromConfig() {
        const Config& config = GlobalConfig().Global();
        s_syncing = true;
        for (size_t i = 0; i < s_cvars.size(); ++i) {
            if (s_cvars[i]) {
                const std::string text = LiveSettingText(static_cast<int>(i), LiveSettingValue(config, static_cast<int>(i)));
                s_cvars[i]->Set(text.c_str(), true, false, false, false);
            }
        }
        s_syncing = false;
    }

    // Both are triggers: Lua sets them to something new each time (the clock), and the callback does the work.
    bool SaveRequested(CVar*, const char*, const char* value, void*) {
        if (!s_syncing && value && value[0] != '\0' && value[0] != '0') {
            const bool saved = GlobalConfig().Save();
            VF_LOG_INFO("settings: saved to %s%s", GlobalConfig().Path().c_str(), saved ? "" : " (not everything could be written)");
        }
        return true;
    }

    bool SyncRequested(CVar*, const char*, const char* value, void*) {
        if (!s_syncing && value && value[0] != '\0' && value[0] != '0') {
            SyncFromConfig();
        }
        return true;
    }

    // "FarFogStretch" -> "Far fog stretch".
    std::string AutoLabel(const char* key) {
        std::string out;
        for (const char* c = key; *c; ++c) {
            if (c != key && *c >= 'A' && *c <= 'Z' && !(c[-1] >= 'A' && c[-1] <= 'Z')) {
                out += ' ';
                out += static_cast<char>(*c - 'A' + 'a');
            } else {
                out += (c == key || (c[-1] >= 'A' && c[-1] <= 'Z')) ? *c : *c;
            }
        }
        return out;
    }

    std::string LongString(const std::string& text) {
        return "[==[" + text + "]==]";
    }

    const Meta* FindMeta(const char* key) {
        for (const Meta& meta : kMeta) {
            if (std::strcmp(meta.key, key) == 0) {
                return &meta;
            }
        }
        return nullptr;
    }

    // A slider step that fits the range.
    double StepFor(double low, double high, bool integer) {
        if (integer) {
            return 1.0;
        }
        const double range = high - low;
        if (range <= 2.0) {
            return 0.01;
        }
        if (range <= 10.0) {
            return 0.05;
        }
        if (range <= 60.0) {
            return 0.1;
        }
        return 1.0;
    }

    std::string Number(double value) {
        char buffer[32];
        std::snprintf(buffer, sizeof buffer, "%.4g", value);
        return buffer;
    }

    // One control of a page.
    void AddControl(std::string& lua, const char* page, int index) {
        const LiveSettingInfo& info = LiveSettingAt(index);
        const Meta* meta = FindMeta(info.key);
        const std::string label = meta ? meta->label : AutoLabel(info.key);
        const std::string help = meta && meta->help[0] ? meta->help : label;
        const std::string cvar = CVarName(info.key);
        lua += std::string("  ") + page + ":";
        if (info.kind == SettingKind::Bool) {
            lua += "Check(" + LongString(cvar) + ", " + LongString(label) + ", " + LongString(help) + ")\n";
            return;
        }
        const bool integer = info.kind == SettingKind::Int;
        const double low = info.lo;
        const double high = info.hi;
        const double step = StepFor(low, high, integer);
        std::string format;
        if (meta && meta->names[0]) {
            // A choice: the readout shows the name (the part before the first colon), the tooltip lists them all.
            std::string names = meta->names;
            std::string table = "{";
            std::string all;
            size_t start = 0;
            int number = static_cast<int>(low);
            while (start <= names.size()) {
                size_t bar = names.find('|', start);
                const std::string one = names.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
                const std::string shortName = one.substr(0, one.find(':'));
                table += "[" + std::to_string(number) + "]=" + LongString(shortName) + ",";
                all += (all.empty() ? "" : "; ") + std::to_string(number) + " " + one;
                ++number;
                if (bar == std::string::npos) {
                    break;
                }
                start = bar + 1;
            }
            table += "}";
            format = "function(v) local n = " + table + "; return n[math.floor(v + 0.5)] or string.format('%d', v + 0.5) end";
            lua += "Slider(" + LongString(cvar) + ", " + LongString(label) + ", " + LongString(help + " (" + all + ")") + ", " + Number(low) + ", " +
                Number(high) + ", 1, " + format + ", 1)\n";
            return;
        }
        if (integer) {
            format = "function(v) return string.format('%d', v + 0.5) end";
        } else {
            const std::string spec = meta && meta->format[0] ? meta->format : "%.2f";
            format = "function(v) return string.format(" + LongString(spec) + ", v) end";
        }
        lua += "Slider(" + LongString(cvar) + ", " + LongString(label) + ", " + LongString(help) + ", " + Number(low) + ", " + Number(high) + ", " +
            Number(step) + ", " + format + ", " + Number(step) + ")\n";
    }

    bool InAnyPage(const char* section) {
        for (const PageSpec& page : kPages) {
            for (const char* s : page.sections) {
                if (s && std::strcmp(s, section) == 0) {
                    return true;
                }
            }
        }
        for (const char* s : kAdvancedSections) {
            if (std::strcmp(s, section) == 0) {
                return true;
            }
        }
        return false;
    }
}

void LumenveilOptions::RegisterCVars() {
    const int count = LiveSettingCount();
    s_cvars.assign(static_cast<size_t>(count), nullptr);
    const Config defaults{};
    // Registering may run the callbacks with the defaults: the config must not take them.
    s_syncing = true;
    for (int i = 0; i < count; ++i) {
        const LiveSettingInfo& info = LiveSettingAt(i);
        const Meta* meta = FindMeta(info.key);
        const std::string name = CVarName(info.key);
        const std::string help = std::string("Lumenveil: ") + (meta ? meta->label : info.key);
        const std::string value = LiveSettingText(i, LiveSettingValue(defaults, i));
        s_cvars[static_cast<size_t>(i)] = CVar::Register(name.c_str(), help.c_str(), 0, value.c_str(), SettingChanged, CATEGORY_GRAPHICS, false,
                                                         reinterpret_cast<void*>(static_cast<intptr_t>(i)), false);
    }
    s_syncing = false;
    // Triggers: not saved, and Lua gives them a new value each time.
    CVar::Register("lumenveilSave", "Lumenveil: write the changed settings to CoAVolFog.ini", 0, "0", SaveRequested, CATEGORY_GRAPHICS, false, nullptr, false);
    CVar::Register("lumenveilSync", "Lumenveil: read the settings from the running config", 0, "0", SyncRequested, CATEGORY_GRAPHICS, false, nullptr, false);
    SyncFromConfig();
    VF_LOG_INFO("settings: %d Lumenveil settings as cvars (lv<Key>), %d with labels from the settings window", count,
                static_cast<int>(sizeof(kMeta) / sizeof(kMeta[0])));
}

std::string LumenveilOptions::BuildScript() {
    const int count = LiveSettingCount();
    std::string lua = "if not VideoOptionsFrame or not WowDecompBuildPage or WowDecompLumenveilPanel then return end\n";
    lua += "local function Page(name, title, subtitle) return WowDecompBuildPage(name, title, subtitle) end\n";
    bool first = true;
    for (const PageSpec& spec : kPages) {
        lua += std::string("do local p = Page(") + LongString(spec.name) + ", " + LongString(spec.title) + ", " + LongString(spec.subtitle) + ")\n";
        if (first) {
            lua += "  WowDecompLumenveilPanel = p\n";
            lua += "  p:Header('Lumenveil')\n";
            lua += "  p:Check('lumenveil', 'Enable Lumenveil', 'Fog, light shafts, night lights, water and wet surfaces. Okay keeps the changes in CoAVolFog.ini; Cancel drops them.')\n";
            first = false;
        }
        for (const char* section : spec.sections) {
            if (!section) {
                break;
            }
            bool header = false;
            for (const Meta& meta : kMeta) {
                if (std::strcmp(meta.section, section) != 0) {
                    continue;
                }
                const int index = FindLiveSetting(meta.key);
                if (index < 0) {
                    continue;
                }
                if (!header) {
                    header = true;
                    lua += std::string("  p:Header(") + LongString(section) + ")\n";
                }
                AddControl(lua, "p", index);
            }
        }
        lua += "  p:Done()\nend\n";
    }
    // The rest: the diagnostics and whatever the settings window does not show.
    lua += "do local p = Page('LumenveilAdvancedPanel', 'Lumenveil Advanced', 'Diagnostics and the settings the Lumenveil window does not show.')\n";
    for (const char* section : kAdvancedSections) {
        bool header = false;
        for (const Meta& meta : kMeta) {
            if (std::strcmp(meta.section, section) != 0) {
                continue;
            }
            const int index = FindLiveSetting(meta.key);
            if (index < 0) {
                continue;
            }
            if (!header) {
                header = true;
                lua += std::string("  p:Header(") + LongString(section) + ")\n";
            }
            AddControl(lua, "p", index);
        }
    }
    bool other = false;
    for (int i = 0; i < count; ++i) {
        const Meta* meta = FindMeta(LiveSettingAt(i).key);
        if (meta && InAnyPage(meta->section)) {
            continue;
        }
        if (!other) {
            other = true;
            lua += "  p:Header('Other')\n";
        }
        AddControl(lua, "p", i);
    }
    lua += "  p:Done()\nend\n";
    // Okay and Apply keep the changes; the pages read the config again whenever the window opens.
    lua += "VideoOptionsFrame:HookScript('OnShow', function() SetCVar('lumenveilSync', tostring(GetTime())) end)\n";
    lua += "for _, name in ipairs({ 'VideoOptionsFrameOkay', 'VideoOptionsFrameApply' }) do\n";
    lua += "  local button = _G[name]\n";
    lua += "  if button then button:HookScript('OnClick', function() SetCVar('lumenveilSave', tostring(GetTime())) end) end\n";
    lua += "end\n";
    // Kept beside the log so a bad page can be read.
    if (FILE* dump = std::fopen("Logs\\LumenveilOptions.lua", "wb")) {
        std::fwrite(lua.data(), 1, lua.size(), dump);
        std::fclose(dump);
    }
    return lua;
}
