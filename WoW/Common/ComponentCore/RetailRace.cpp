#include "WoW/Common/ComponentCore/RetailRace.h"

#if defined(WOWDECOMP_RETAIL_ASSETS)

#include "WoW/Common/ComponentCore/CharacterComponent.h"
#include "WoW/Common/TextureCache.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/DB/Rec/HelmetGeosetVisDataRec.h"
#include "WoW/Source/DB/Rec/ItemDisplayInfoRec.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "Engine/Source/Base/Handle.h"
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include "Engine/Source/Gx/CGxTexFlags.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Retail/Appearance.h"
#include "Engine/Source/Retail/BusyNotice.h"
#include "Engine/Source/Retail/CascSource.h"
#include "Engine/Source/Retail/Compose.h"
#include "Engine/Source/Retail/Equipment.h"
#include "Engine/Source/Retail/RaceGeometry.h"
#include "Engine/Source/Retail/RetailModel.h"
#include "Engine/Source/Services/Texture.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "WoW/Source/Glue/CGlueLoading.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <unordered_map>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace app = Retail::Db2::appearance;

namespace {

// Diagnostics: Logs\RetailRace.log, one line per notable step with a millisecond tick.
void Log(const char* format, ...) {
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    FILE* file = std::fopen("Logs/RetailRace.log", "a");
    if (!file) return;
    std::fprintf(file, "%08lu ", static_cast<unsigned long>(GetTickCount()));
    va_list args;
    va_start(args, format);
    std::vfprintf(file, format, args);
    va_end(args);
    std::fputc(10, file);
    std::fclose(file);
}

std::map<uint32_t, uint32_t>& RaceMap() {
    static std::map<uint32_t, uint32_t> map;
    static std::once_flag once;
    std::call_once(once, [] {
        auto add = [](uint32_t wrath, uint32_t retail) { if (wrath && retail) map[wrath] = retail; };
        if (const char* env = std::getenv("WOW_RETAIL_RACES")) {
            for (const char* c = env; *c;) {
                char* end = nullptr;
                const uint32_t wrath = uint32_t(std::strtoul(c, &end, 10));
                if (*end != ':') break;
                c = end + 1;
                add(wrath, uint32_t(std::strtoul(c, &end, 10)));
                c = *end == ',' ? end + 1 : end;
                if (c == end && *c) break;
            }
            return;
        }
#ifdef _WIN32
        char exe[MAX_PATH];
        if (GetModuleFileNameA(nullptr, exe, sizeof(exe))) {
            std::string path = exe;
            path = path.substr(0, path.find_last_of("\\/") + 1) + "Data\\Retail\\RetailRaces.csv";
            std::ifstream csv(path);
            std::string line;
            while (std::getline(csv, line)) {
                unsigned wrath = 0, retail = 0;
                if (std::sscanf(line.c_str(), "%u;%u", &wrath, &retail) == 2) add(wrath, retail);
            }
        }
#endif
    });
    return map;
}

uint32_t RetailRaceId(uint32_t race) {
    const auto found = RaceMap().find(race);
    return found == RaceMap().end() ? 0 : found->second;
}

// The option of a race/sex model whose display name is `name` (0 if none).
uint32_t OptionByName(uint32_t retailRace, uint32_t sex, const char* name) {
    const uint32_t model = app::ChrModelForRace(retailRace, sex);
    for (uint32_t i = 0, n = app::OptionCount(model); i < n; ++i) {
        app::ChrOption option{};
        if (app::OptionAt(model, i, option) && app::OptionName(option.id) == name) return option.id;
    }
    return 0;
}

// The stock selectors (skin, face, hair style, hair colour, facial hair) mapped onto Retail options by name.
const char* FacialOptionName(uint32_t retailRace, uint32_t sex) {
    // Wrath female Dwarves use the fifth appearance byte for earrings, not Human makeup.
    return sex ? (retailRace == 3 ? "Earrings" : "Makeup") : "Beard";
}

const char* StockOptionName(uint32_t variation, uint32_t retailRace, uint32_t sex, bool colors) {
    switch (variation) {
        case VARIATION_SKIN: return "Skin Color";
        case VARIATION_FACE: return "Face";
        case VARIATION_HAIR: return colors ? "Hair Color" : "Hair Style";
        case VARIATION_FACIAL_HAIR: return FacialOptionName(retailRace, sex);
        default: return nullptr;
    }
}

// ---- option model --------------------------------------------------------------------------------------------

enum class Stock { None, Skin, Face, HairStyle, HairColor, Facial };

struct OptionInfo {
    uint32_t id = 0;
    std::string name;
    uint32_t count = 0;
    Stock stock = Stock::None;
};

Stock StockFor(const std::string& name, uint32_t retailRace, uint32_t sex) {
    if (name == "Skin Color") return Stock::Skin;
    if (name == "Face") return Stock::Face;
    if (name == "Hair Style") return Stock::HairStyle;
    if (name == "Hair Color") return Stock::HairColor;
    if (name == FacialOptionName(retailRace, sex)) return Stock::Facial;
    return Stock::None;
}

// The options of a race/sex model in display order, with their choice counts.
const std::vector<OptionInfo>& OptionsFor(uint32_t retailRace, uint32_t sex) {
    static std::map<uint64_t, std::vector<OptionInfo>> cache;
    std::vector<OptionInfo>& options = cache[(uint64_t(retailRace) << 8) | sex];
    if (!options.empty()) return options;
    const uint32_t model = app::ChrModelForRace(retailRace, sex);
    for (uint32_t i = 0, n = app::OptionCount(model); i < n; ++i) {
        app::ChrOption option{};
        if (!app::OptionAt(model, i, option)) continue;
        OptionInfo info;
        info.id = option.id;
        info.name = app::OptionName(option.id);
        info.count = app::ChoiceCount(option.id);
        info.stock = StockFor(info.name, retailRace, sex);
        options.push_back(std::move(info));
    }
    return options;
}

uint32_t* PreferenceFor(CharacterPreferences& prefs, Stock stock) {
    switch (stock) {
        case Stock::Skin: return &prefs.m_skinID;
        case Stock::Face: return &prefs.m_faceId;
        case Stock::HairStyle: return &prefs.m_hairStyleId;
        case Stock::HairColor: return &prefs.m_hairColorId;
        case Stock::Facial: return &prefs.m_facialHairId;
        default: return nullptr;
    }
}

// Selected choice index per option for components whose options go beyond the five stock selectors.
std::unordered_map<CCharacterComponent*, std::vector<uint32_t>> g_state;

// Retail components seen by the geometry hook; ticked each frame in the world.
std::set<CCharacterComponent*> g_active;

uint32_t IndexOf(CCharacterComponent* character, const std::vector<OptionInfo>& options, size_t i) {
    if (!options[i].count) return 0;
    uint32_t value = 0;
    if (options[i].stock != Stock::None) {
        value = *PreferenceFor(character->m_data.m_preferences, options[i].stock);
    } else {
        const auto found = g_state.find(character);
        if (found != g_state.end() && i < found->second.size()) value = found->second[i];
    }
    return value % options[i].count;
}

void StoreIndex(CCharacterComponent* character, const std::vector<OptionInfo>& options, size_t i, uint32_t value) {
    if (!options[i].count) return;
    value %= options[i].count;
    if (options[i].stock != Stock::None) {
        *PreferenceFor(character->m_data.m_preferences, options[i].stock) = value;
        return;
    }
    std::vector<uint32_t>& state = g_state[character];
    state.resize(options.size(), 0);
    state[i] = value;
}

// Choice IDs for a character, one per option. WOW_RETAIL_CHOICES ("id,id,...") overrides everything, for testing.
std::vector<uint32_t> Choices(uint32_t retailRace, CCharacterComponent* character) {
    std::vector<uint32_t> choices;
    if (const char* env = std::getenv("WOW_RETAIL_CHOICES")) {
        for (const char* c = env; *c;) {
            char* end = nullptr;
            choices.push_back(uint32_t(std::strtoul(c, &end, 10)));
            if (end == c) break;
            c = *end == ',' ? end + 1 : end;
        }
        return choices;
    }
    const std::vector<OptionInfo>& options = OptionsFor(retailRace, character->m_data.m_preferences.m_sexID);
    for (size_t i = 0; i < options.size(); ++i) {
        app::ChrChoice choice{};
        if (options[i].count && app::ChoiceAt(options[i].id, IndexOf(character, options, i), choice)) choices.push_back(choice.id);
    }
    return choices;
}

// The shared model remembers the name it was created from; Retail models are created from "retail/<id>.m2".
bool IsRetailModel(CM2Model* model) {
    if (!model || !model->m_shared) return false;
    const char* name = model->m_shared->m_fileName;
    return !_strnicmp(name, "retail", 6) && (name[6] == '/' || name[6] == 92);
}

// The creator and character select run without an active player. In the world the stock validation applies.
bool InWorld() {
    return ClntObjMgrGetActivePlayer().m_guid != 0;
}

bool FetchBlp(uint32_t fileDataId, std::vector<uint8_t>& bytes, void*) {
    std::string error;
    return Retail::Source().Read(Retail::FileName(fileDataId, ".blp").c_str(), bytes, error);
}

// Everything derived from (race, sex, choices): the recipe and its composed images, kept in memory.
struct Look {
    app::Recipe recipe;
    std::vector<Retail::ComposedTexture> composed;
    std::map<uint32_t, CTexture*> textures; // finished GPU textures for types that carry no equipment
};

Look& LookFor(uint32_t retail, uint32_t sex, const std::vector<uint32_t>& choices, std::string& key) {
    static std::map<std::string, Look> looks;
    static std::deque<std::string> order;
    key = std::to_string(retail) + ":" + std::to_string(sex);
    for (const uint32_t choice : choices) key += ":" + std::to_string(choice);
    auto found = looks.find(key);
    if (found != looks.end()) return found->second;

    Retail::BusyNotice busy("Preparing Retail character data");
    const unsigned long start = GetTickCount();
    // Bound memory while the player cycles through options: keep a few recent looks.
    while (order.size() >= 6) {
        const auto old = looks.find(order.front());
        if (old != looks.end()) {
            for (auto& texture : old->second.textures) HandleClose(reinterpret_cast<HOBJECT>(texture.second));
            looks.erase(old);
        }
        order.pop_front();
    }
    order.push_back(key);
    Look& look = looks[key];
    if (app::BuildForCharacter(retail, sex, choices.data(), uint32_t(choices.size()), look.recipe)) {
        std::string warnings;
        Retail::ComposeTextures(look.recipe, FetchBlp, nullptr, look.composed, warnings);
        if (!warnings.empty()) Log("compose warnings: %s", warnings.c_str());
    }
    Log("look %s: model %u geosets %zu layers %zu composed %zu in %lu ms", key.c_str(), look.recipe.modelFileDataId,
        look.recipe.geosets.size(), look.recipe.layers.size(), look.composed.size(), GetTickCount() - start);
    return look;
}

CTexture* MakeTexture(const std::string& name, const Retail::Image& image) {
    std::vector<uint8_t> blp;
    Retail::BuildBlp(image, blp);
    CGxTexFlags flags(GxTex_LinearMipNearest, 0, 0, 0, 0, 0, 1);
    return TextureCreateFromBlpData(name.c_str(), flags, blp.data());
}

// Body texture: the retail skin sheet with the equipped items' section layers pasted over it. The stock item
// section grid (512x512) is the left half of the retail sheet at 2x, in the same section order.
void BindBody(CCharacterComponent* character, CM2Model* model, const Look& look, const std::string& key) {
    const Retail::ComposedTexture* body = nullptr;
    for (const auto& texture : look.composed)
        if (texture.textureType == 1) body = &texture;
    if (!body) { Log("no body sheet for %s", key.c_str()); return; }

    std::string itemKey;
    const auto layers = Retail::CollectEquipmentLayers(character->m_section, (character->m_flags & 0x20) != 0);
    for (const auto& layer : layers) {
        itemKey += "/" + std::to_string(layer.section) + "." + std::to_string(layer.layer) + "." + std::to_string(layer.displayId);
        // Guild tabard layers all have display ID zero; their filenames identify the emblem/colors.
        if (!layer.displayId && layer.texture) {
            itemKey += ":" + std::string(static_cast<const CACHEENTRY*>(layer.texture)->m_fileName);
        }
    }

    static std::map<std::string, CTexture*> bodies;
    static std::deque<std::string> order;
    const std::string name = "Retail\\Race\\" + key + "\\body" + itemKey;
    auto found = bodies.find(name);
    if (found == bodies.end()) {
        Retail::BusyNotice busy("Preparing Retail character data");
        const unsigned long start = GetTickCount();
        Retail::Image sheet = body->image;
        for (const auto& layer : layers) {
            void* handle = layer.texture;
            // Only dirty sections reload their item textures. A full retail sheet also needs the
            // unchanged sections, whose temporary handles were released by the previous render.
            const bool reload = !handle;
            if (reload) {
                ItemDisplayInfoRec display;
                if (g_itemDisplayInfoDB.GetRecord(layer.displayId, &display)) {
                    handle = character->LoadItemComponentTexture(&display, static_cast<COMPONENT_SECTIONS>(layer.section));
                }
            }
            TCTEXTUREINFO info{};
            TextureCacheGetInfo(handle, info, 1);
            const CACHEENTRY* entry = static_cast<const CACHEENTRY*>(handle);
            std::vector<uint8_t> bytes;
            if (entry && !entry->m_loadFailed && entry->m_data && info.m_width) {
                const auto* begin = reinterpret_cast<const uint8_t*>(entry->m_data);
                bytes.assign(begin, begin + entry->m_size);
            }
            if (reload) TextureCacheDestroyTexture(handle);
            Retail::Image item;
            std::string error;
            if (!Retail::DecodeBlp(bytes, item, error)) {
                // Keep the previous complete sheet rather than caching an incomplete equipment state.
                Log("equipment decode failed: section %u layer %u display %u: %s", layer.section, layer.layer, layer.displayId, error.c_str());
                return;
            }
            for (uint32_t i = 0, n = app::SectionCount(look.recipe.textureLayoutId); i < n; ++i) {
                app::TextureSection section{};
                if (app::SectionAt(look.recipe.textureLayoutId, i, section) && section.sectionType == layer.section)
                    Retail::PaintImage(sheet, item, section.x, section.y, section.width, section.height, 2);
            }
        }
        CTexture* texture = MakeTexture(name, sheet);
        Log("body %s: %zu item layers, texture %p, %lu ms, model flags %u", name.c_str(), layers.size(),
            static_cast<void*>(texture), GetTickCount() - start, unsigned(model->m_fFlags));
        if (!texture) return;
        found = bodies.emplace(name, texture).first;
        order.push_back(name);
        // Keep a handful of equipment states; each is a full-size sheet.
        while (order.size() > 8) {
            const auto old = bodies.find(order.front());
            if (old != bodies.end()) { HandleClose(reinterpret_cast<HOBJECT>(old->second)); bodies.erase(old); }
            order.pop_front();
        }
    }
    model->ReplaceTexture(1, found->second);
}

bool IdentityHelmetSocket(const M2CompBone& bone) {
    if (bone.m_translationTrack.m_sequenceTimes.m_count || bone.m_scaleTrack.m_sequenceTimes.m_count) return false;
    const auto& rotation = bone.m_rotationTrack;
    if (!rotation.m_sequenceTimes.m_count) return true;
    // Male Dwarf has two identical global identity keys. Only global tracks are fully resident
    // here; a per-sequence track may still point into an animation that has not downloaded.
    if (rotation.m_loopIndex == 0xFFFF || rotation.m_trackType > 1 || !rotation.m_sequenceKeys.m_count) return false;
    for (uint32_t i = 0; i < rotation.m_sequenceKeys.m_count; ++i) {
        const auto& keys = rotation.m_sequenceKeys[i];
        if (!keys.m_count) return false;
        for (uint32_t j = 0; j < keys.m_count; ++j)
            if (!Retail::IsIdentityHelmetRotation(keys[j].m_values)) return false;
    }
    return true;
}

void FitLegacyHelmet(CCharacterComponent* character) {
    const auto& prefs = character->m_data.m_preferences;
    if ((prefs.m_raceID != 1 && prefs.m_raceID != 3) || RetailRaceId(prefs.m_raceID) != prefs.m_raceID || prefs.m_sexID > 1
        || !character->m_itemDisplayID[ITEMSLOT_HEAD] || !RetailRaceActive(character)) return;
    CM2Model* parent = character->m_data.m_model;
    if (!(parent->m_fFlags & 1)) return; // Tick retries once the asynchronous parent load completes.
    CM2Model* helmet = parent->m_attachList;
    while (helmet && helmet->m_attachmentId != ATTACH_HELM) helmet = helmet->m_attachNext;
    if (!helmet || IsRetailModel(helmet)) return; // This correction is for installed legacy helmets.

    const M2Data* data = parent->m_shared->m_data;
    if (data->m_boneIndicesById.m_count <= 6 || data->m_attachmentIndicesById.m_count <= ATTACH_HELM) return;
    const uint16_t head = data->m_boneIndicesById[6];
    const uint16_t socket = data->m_attachmentIndicesById[ATTACH_HELM];
    if (head >= data->m_bones.m_count || socket >= data->m_attachments.m_count) return;
    const auto& attachment = data->m_attachments[socket];
    if (attachment.m_bone >= data->m_bones.m_count) return;
    const auto& socketBone = data->m_bones[attachment.m_bone];
    // The supported Human/Dwarf helpers share the head frame. Retain this guard for other rigs.
    if (socketBone.m_parentIndex != head || !IdentityHelmetSocket(socketBone)) return;

    const char* donorPath = prefs.m_raceID == 3
        ? (prefs.m_sexID ? "Character\\Dwarf\\Female\\DwarfFemale.m2" : "Character\\Dwarf\\Male\\DwarfMale.m2")
        : (prefs.m_sexID ? "Character\\Human\\Female\\HumanFemale.m2" : "Character\\Human\\Male\\HumanMale.m2");
    struct Donor { bool attempted = false, valid = false; Retail::HelmetAnchor anchor; };
    static std::map<std::string, Donor> donors;
    Donor& donor = donors[donorPath];
    if (!donor.attempted) {
        donor.attempted = true;
        void* bytes = nullptr;
        uint32_t size = 0;
        std::string error;
        if (SFile::LoadFile(donorPath, &bytes, &size, 0, nullptr)) {
            const auto* begin = static_cast<const uint8_t*>(bytes);
            donor.valid = Retail::ReadHelmetAnchor(std::vector<uint8_t>(begin, begin + size), donor.anchor, error);
            SFile::Unload(bytes);
        } else error = "could not read the installed helmet donor model";
        if (!donor.valid) Log("helmet donor %s: %s", donorPath, error.c_str());
    }
    if (!donor.valid) return;

    const auto& headPivot = data->m_bones[head].m_pivot;
    Retail::HelmetAnchor target{{headPivot.x, headPivot.y, headPivot.z},
        {attachment.m_position.x, attachment.m_position.y, attachment.m_position.z}};
    const auto shift = Retail::HelmetTranslation(donor.anchor, target);
    if ((helmet->m_fFlags & 0x8000) && helmet->m_worldTransform.d0 == shift[0]
        && helmet->m_worldTransform.d1 == shift[1] && helmet->m_worldTransform.d2 == shift[2]) return;
    // An attached model's worldTransform is local to its socket. Set an absolute local correction,
    // never accumulate it: the existing head-bone animation supplies rotation, translation and scale.
    helmet->SetWorldTransform(C3Vector(shift[0], shift[1], shift[2]), 0.0f, 1.0f);
    Log("helmet fit race %u sex %u display %u model %s donor %s local shift %.6f %.6f %.6f",
        prefs.m_raceID, prefs.m_sexID, character->m_itemDisplayID[ITEMSLOT_HEAD], helmet->m_shared->m_fileName,
        donorPath, shift[0], shift[1], shift[2]);
}

}

bool RetailRaceMapped(uint32_t race) {
    return RetailRaceId(race) != 0;
}

bool RetailRaceActive(CCharacterComponent* character) {
    return character && RetailRaceId(character->m_data.m_preferences.m_raceID) && IsRetailModel(character->m_data.m_model);
}

bool RetailRaceOwns(uint32_t race, uint32_t sex) {
    const uint32_t retail = RetailRaceId(race);
    return retail && app::ChrModelForRace(retail, sex);
}

bool RetailRaceModelPath(uint32_t race, uint32_t sex, char* path, size_t size) {
    const uint32_t retail = RetailRaceId(race);
    if (!retail) return false;
    Retail::BusyNotice busy("Loading Retail race data");
    app::Recipe recipe;
    const bool ok = app::BuildForCharacter(retail, sex, nullptr, 0, recipe) && recipe.modelFileDataId;
    Log("model path race %u sex %u -> %s (%u)", race, sex, ok ? "ok" : "unavailable", recipe.modelFileDataId);
    if (!ok) return false;
    std::snprintf(path, size, "%s", Retail::FileName(recipe.modelFileDataId, ".m2").c_str());
    return true;
}

bool RetailRaceBody(CCharacterComponent* character) {
    CharacterPreferences& prefs = character->m_data.m_preferences;
    CM2Model* model = character->m_data.m_model;
    const uint32_t retail = RetailRaceId(prefs.m_raceID);
    if (!retail || !IsRetailModel(model)) return false;
    const std::vector<uint32_t> choices = Choices(retail, character);
    std::string key;
    Look& look = LookFor(retail, prefs.m_sexID, choices, key);
    Log("RetailRaceBody component %p key %s", static_cast<void*>(character), key.c_str());
    BindBody(character, model, look, key);
    return true;
}

bool RetailRaceGeometry(CCharacterComponent* character) {
    CharacterPreferences& prefs = character->m_data.m_preferences;
    CM2Model* model = character->m_data.m_model;
    const uint32_t retail = RetailRaceId(prefs.m_raceID);
    if (!retail || !IsRetailModel(model)) return false;

    const std::vector<uint32_t> choices = Choices(retail, character);
    std::string key;
    Look& look = LookFor(retail, prefs.m_sexID, choices, key);
    if (!look.recipe.modelFileDataId) return false;
    Log("RetailRaceGeometry component %p key %s", static_cast<void*>(character), key.c_str());
    g_active.insert(character);

    // Stock only hides geosets up to id 2000. Retail models carry more groups above it (feet 20xx, torso 22xx,
    // customization 3xxx...), all of which would otherwise draw at once. Show just the defaults here; the
    // recipe and equipment add the rest.
    model->SetGeometryVisible(2001, 9999, 0);
    // Restore anatomy that no customization choice selects, including Dwarves' full head (32).
    for (const uint16_t base : Retail::DefaultRaceGeosets(look.recipe.geosets))
        model->SetGeometryVisible(base, base, 1);

    // Stock geoset slots are indexed by group; recipe geosets replace the Wrath hair/beard defaults.
    for (const uint16_t geoset : look.recipe.geosets) {
        model->SetGeometryVisible(geoset, geoset, 1);
        if (geoset / 100 < 19) character->m_data.m_geosets[geoset / 100] = geoset;
    }

    // Hair, eyes and other single-layer types are cached per look; the body also carries equipment.
    for (const auto& texture : look.composed) {
        if (texture.textureType == 1) continue;
        CTexture*& bound = look.textures[texture.textureType];
        if (!bound) bound = MakeTexture("Retail\\Race\\" + key + "\\" + std::to_string(texture.textureType), texture.image);
        if (bound) model->ReplaceTexture(texture.textureType, bound);
    }
    // The body is composed after item loading/section prep. Geometry-only refreshes happen after
    // temporary equipment textures are freed and must never replace the equipped sheet with bare skin.
    return true;
}

void RetailRaceEquipment(CCharacterComponent* character) {
    CM2Model* model = character->m_data.m_model;
    if (!RetailRaceActive(character)) return;
    FitLegacyHelmet(character);

    // Bare feet and closed shoes are separate Retail meshes. Wrath boot style zero is still a
    // shoe; shin style and robe coverage must not select bare toes under its painted texture.
    bool hasClosedFoot = true;
    if ((model->m_fFlags & 1) && model->m_shared->m_skinData) {
        hasClosedFoot = false;
        const auto& sections = model->m_shared->m_skinData->m_skinSections;
        for (uint32_t i = 0; i < sections.m_count; ++i) {
            hasClosedFoot = hasClosedFoot || (sections[i].m_skinSectionId & 0xFFFF) == 2002;
        }
    }
    Retail::ApplyLegacyFootGeometry(*model, character->m_itemDisplayID[ITEMSLOT_BOOTS], hasClosedFoot);

    // AddHelm sets stock selector values before the recipe runs. Apply the effective target's
    // helmet hides last; otherwise retail hair and facial hair reappear underneath the helm.
    const uint32_t race = character->m_data.m_preferences.m_raceID;
    const uint32_t sex = character->m_data.m_preferences.m_sexID;
    ItemDisplayInfoRec display;
    if (race >= 32 || sex > 1 || !character->m_itemDisplayID[ITEMSLOT_HEAD]
        || !g_itemDisplayInfoDB.GetRecord(character->m_itemDisplayID[ITEMSLOT_HEAD], &display)) return;
    const auto* visibility = g_helmetGeosetVisDataDB.GetRecord(display.m_helmetGeosetVisID[sex]);
    if (!visibility) return;
    constexpr uint32_t groups[] = {0, 1, 2, 3, 7, 16, 17};
    for (uint32_t i = 0; i < 7; ++i) {
        if (!(uint32_t(visibility->m_hideGeoset[i]) & (uint32_t{1} << race))) continue;
        const uint32_t group = groups[i];
        // Geoset zero contains unconditional anatomy, while 1..99 are hairstyles.
        model->SetGeometryVisible(group ? group * 100 : 1, group * 100 + 99, 0);
    }
}

void RetailRaceForget(CCharacterComponent* character) {
    g_state.erase(character);
    g_active.erase(character);
}

namespace {

// Same setter sequence the stock initializer uses, then rebuild geosets and textures.
void Refresh(CCharacterComponent* character) {
    CharacterPreferences& prefs = character->m_data.m_preferences;
    character->SetHairColor(prefs.m_hairColorId, false, "");
    character->SetSkinColor(prefs.m_skinID, false, true, "");
    character->SetFace(prefs.m_faceId, false, "");
    character->SetHairStyle(prefs.m_hairStyleId, "");
    character->SetBeardStyle(prefs.m_facialHairId, false, "");
    character->m_flags |= 0x4;
    character->m_dirtySections = 0xFFFFFFFF;
    CGlueLoading::StartLoad(character, 1);
}

struct Snapshot {
    CCharacterComponent* owner = nullptr;
    uint32_t prefs[5] = {};
    std::vector<uint32_t> state;
};

Snapshot g_preview;

}

int RetailRaceCycleCommand(lua_State* L, CCharacterComponent* character, const char* command) {
    if (!character) return -1;
    CharacterPreferences& prefs = character->m_data.m_preferences;
    const uint32_t retail = RetailRaceId(prefs.m_raceID);
    const bool owned = retail && app::ChrModelForRace(retail, prefs.m_sexID) && IsRetailModel(character->m_data.m_model);

    if (!std::strcmp(command, "EA_RETAIL")) {
        lua_pushnumber(L, owned ? 1 : 0);
        return 1;
    }
    if (!owned) return -1;

    const std::vector<OptionInfo>& options = OptionsFor(retail, prefs.m_sexID);

    if (!std::strcmp(command, "EA_PREVIEW_SAVE") || !std::strcmp(command, "EA_PREVIEW_RESTORE") || !std::strcmp(command, "EA_PREVIEW_END")) {
        if (!std::strcmp(command, "EA_PREVIEW_SAVE")) {
            g_preview.owner = character;
            g_preview.prefs[0] = prefs.m_skinID; g_preview.prefs[1] = prefs.m_faceId; g_preview.prefs[2] = prefs.m_hairStyleId;
            g_preview.prefs[3] = prefs.m_hairColorId; g_preview.prefs[4] = prefs.m_facialHairId;
            const auto found = g_state.find(character);
            g_preview.state = found == g_state.end() ? std::vector<uint32_t>() : found->second;
            lua_pushnumber(L, 1);
        } else if (!std::strcmp(command, "EA_PREVIEW_END")) {
            g_preview.owner = nullptr;
            lua_pushnumber(L, 1);
        } else if (g_preview.owner == character) {
            prefs.m_skinID = g_preview.prefs[0]; prefs.m_faceId = g_preview.prefs[1]; prefs.m_hairStyleId = g_preview.prefs[2];
            prefs.m_hairColorId = g_preview.prefs[3]; prefs.m_facialHairId = g_preview.prefs[4];
            g_state[character] = g_preview.state;
            Refresh(character);
            lua_pushnumber(L, 1);
        } else {
            lua_pushnumber(L, 0);
        }
        return 1;
    }

    if (!std::strcmp(command, "EA_RANDOM")) {
        static std::mt19937 random(GetTickCount());
        for (size_t i = 0; i < options.size(); ++i)
            if (options[i].count > 1) StoreIndex(character, options, i, std::uniform_int_distribution<uint32_t>(0, options[i].count - 1)(random));
        Refresh(character);
        return 0;
    }

    if (!lua_isnumber(L, 2)) return -1;
    const double requested = lua_tonumber(L, 2);
    const bool isGet = !std::strcmp(command, "EA_GET");
    const bool inRange = requested >= 1 && requested <= double(options.size()) && requested == double(int(requested));
    if (!inRange) {
        if (isGet) { lua_pushnumber(L, 0); lua_pushnumber(L, 1); lua_pushstring(L, ""); return 3; }
        if (!std::strcmp(command, "EA_CHOICES") || !std::strcmp(command, "EA_CHOICE_NAME")) { lua_pushstring(L, ""); return 1; }
        return 0;
    }
    const size_t index = size_t(requested) - 1;
    const OptionInfo& option = options[index];
    const uint32_t value = IndexOf(character, options, index);

    if (isGet) {
        lua_pushnumber(L, value);
        lua_pushnumber(L, option.count);
        lua_pushstring(L, option.name.c_str());
        return 3;
    }
    if (!std::strcmp(command, "EA_CHOICES")) {
        std::string list;
        for (uint32_t i = 0; i < option.count; ++i) list += std::to_string(i) + ",";
        lua_pushstring(L, list.c_str());
        return 1;
    }
    if (!std::strcmp(command, "EA_CHOICE_NAME")) {
        app::ChrChoice choice{};
        const double which = lua_isnumber(L, 3) ? lua_tonumber(L, 3) : -1;
        const std::string name = which >= 0 && which < option.count && app::ChoiceAt(option.id, uint32_t(which), choice) ? app::ChoiceName(choice.id) : std::string();
        lua_pushstring(L, name.c_str());
        return 1;
    }
    if (!std::strcmp(command, "EA_SET") || !std::strcmp(command, "EA_CYCLE")) {
        const double argument = lua_tonumber(L, 3);
        if (!std::isfinite(argument) || !option.count) return 0;
        const uint32_t next = !std::strcmp(command, "EA_SET")
            ? (argument < 0 ? 0 : uint32_t(argument))
            : uint32_t((int64_t(value) + (argument < 0 ? -1 : 1) + option.count) % option.count);
        if (next >= option.count) return 0;
        StoreIndex(character, options, index, next);
        Refresh(character);
        return 0;
    }
    return -1;
}

void RetailRaceTick() {
    if (g_active.empty()) return;
    // Prep() queues a component whose sections are dirty (equipment changed, textures lost) and runs the geometry
    // prep when only geosets changed; both end in the retail hooks.
    const bool inWorld = InWorld();
    for (CCharacterComponent* character : std::set<CCharacterComponent*>(g_active)) {
        FitLegacyHelmet(character);
        if (inWorld) character->Prep();
    }
}

// Counts the stock selectors report for a Retail race: the size of the mapped Retail option.
int32_t RetailRaceSectionCount(int32_t race, int32_t sex, int32_t variation, bool colors) {
    if (InWorld()) return -1;
    const uint32_t retail = RetailRaceId(uint32_t(race));
    if (!retail || sex < 0 || sex > 1 || !app::ChrModelForRace(retail, uint32_t(sex))) return -1;
    if (variation == VARIATION_SKIN && !colors) return 1;   // one "variation" whose colours are the skins
    if (variation == VARIATION_UNDERWEAR) return -1;
    const bool colourCount = colors && (variation == VARIATION_SKIN || variation == VARIATION_HAIR);
    if (colors && !colourCount) return 1;                   // faces and facial hair have no colour dimension
    const char* name = StockOptionName(uint32_t(variation), retail, uint32_t(sex), colors);
    if (!name) return -1;
    const uint32_t option = OptionByName(retail, uint32_t(sex), name);
    return option ? int32_t(app::ChoiceCount(option)) : 0;
}

// Stock CharSections lookups index Wrath rows that do not describe a Retail look; point them at row 0 so the
// stock validation passes and the real choice is applied through the Retail tables.
void RetailRaceSectionArguments(int32_t race, int32_t sex, int32_t* style, int32_t* color) {
    if (RetailRaceSectionCount(race, sex, VARIATION_FACE, false) < 0) return;
    *style = 0;
    *color = 0;
}

#else

bool RetailRaceMapped(uint32_t) { return false; }
bool RetailRaceActive(CCharacterComponent*) { return false; }
bool RetailRaceOwns(uint32_t, uint32_t) { return false; }
bool RetailRaceModelPath(uint32_t, uint32_t, char*, size_t) { return false; }
bool RetailRaceGeometry(CCharacterComponent*) { return false; }
bool RetailRaceBody(CCharacterComponent*) { return false; }
void RetailRaceTick() {}
void RetailRaceEquipment(CCharacterComponent*) {}
int RetailRaceCycleCommand(lua_State*, CCharacterComponent*, const char*) { return -1; }
void RetailRaceForget(CCharacterComponent*) {}
int32_t RetailRaceSectionCount(int32_t, int32_t, int32_t, bool) { return -1; }
void RetailRaceSectionArguments(int32_t, int32_t, int32_t*, int32_t*) {}

#endif
