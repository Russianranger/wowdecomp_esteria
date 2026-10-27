#ifndef ENGINE_SOURCE_RETAIL_ETHEREAL_H
#define ENGINE_SOURCE_RETAIL_ETHEREAL_H

#include "Engine/Source/Retail/Appearance.h"
#include "Engine/Source/Retail/SlimeGiant.h"
#include "Engine/Source/Retail/EtherealEffects.h"
#include "Engine/Source/Retail/EtherealCustomColors.h"
#include "Engine/Source/Tempest/CImVector.h"
#include <array>
#include <cmath>
#include <map>
#include <set>

namespace Retail::Ethereal {
constexpr uint32_t Profile = 0xf0030000u;
constexpr uint32_t Bodies[] = {5926159, 6152557, 6254042};
constexpr uint16_t BareHandGeoset = 6303;
constexpr uint16_t SourceGeosets[] = {
    101, 102, 103, 201, 202, 203, 301, 302, 303, 304, 305, 306, 307, 308,
    309, 310, 311, 312, 401, 402, 403, 501, 502, 503, 504, 601, 602
};
constexpr uint32_t MaleCloth[] = {6190261, 6190257, 6190260, 6190262, 6190263, 6190264,
                                 6190900, 6190902, 6190903,
                                 LocalTexture, LocalTexture+1, LocalTexture+2, LocalTexture+3, LocalTexture+4,
                                 LocalTexture+5, LocalTexture+6, LocalTexture+7, LocalTexture+8, LocalTexture+9};
constexpr uint32_t FemaleCloth[] = {6190902, 6190261, 6190900, 6190901, 6190903, 6190904, 6190905,
                                   LocalTexture, LocalTexture+1, LocalTexture+2, LocalTexture+3, LocalTexture+4,
                                   LocalTexture+5, LocalTexture+6, LocalTexture+7, LocalTexture+8, LocalTexture+9};
constexpr uint32_t MaleBandages[] = {6190259, 6190258};
constexpr uint32_t FemaleBandages[] = {6190259, 6190257, 6190258};
constexpr uint32_t EnergyColors[] = {6190251, 6190249, 6190250, 6190252, 6190253, 6190254};
constexpr uint32_t ExtraColors[] = {6190256, 6190255,
    LocalTexture+10, LocalTexture+11, LocalTexture+12, LocalTexture+13, LocalTexture+14,
    LocalTexture+15, LocalTexture+16, LocalTexture+17, LocalTexture+18, LocalTexture+19};
constexpr uint32_t MechColors[] = {6403197, 6403194, 6403195, 6403196, 6403198, 6403199,
    LocalTexture+20, LocalTexture+21, LocalTexture+22, LocalTexture+23, LocalTexture+24,
    LocalTexture+25, LocalTexture+26, LocalTexture+27, LocalTexture+28, LocalTexture+29};
constexpr uint32_t MechGlowColors[] = {6435135, 6435132, 6435133, 6435134, 6435136, 6435137,
    LocalTexture+30, LocalTexture+31, LocalTexture+32, LocalTexture+33, LocalTexture+34,
    LocalTexture+35, LocalTexture+36, LocalTexture+37, LocalTexture+38, LocalTexture+39};
constexpr const char* MechColorNames[] = {"Red", "Black", "Magenta", "Purple", "Teal", "Umber"};

inline bool Race(uint32_t race) { return race == 77 || race == 78; }
inline bool HidesArmor(uint32_t race, uint32_t form) {
    return Race(race) && form == Db2::appearance::FormDreadnaught;
}
inline void FitGloveSection(Db2::appearance::TextureSection& section) {
    const uint32_t height = section.height / 2;
    section.y += section.height - height;
    section.height = height;
}
inline constexpr uint32_t ArmorSockets[] = {5,6,11};
template <class Unit>
inline void SyncWieldedHands(Unit& unit) {
    if (unit.m_sheatheState != 1 || !unit.m_worldModel) return;
    for (uint32_t hand = 0; hand < 2; ++hand) {
        const auto* item = unit.GetVirtualItem(hand,false);
        if (!item) continue;
        // Wrath inventory type 14 is a shield; the hand attach IDs are right 1, left 2, shield 0.
        const uint32_t socket = !hand ? 1 : item->m_inventoryType == 14 ? 0 : 2;
        bool wielded = false;
        for (auto* child = unit.m_worldModel->m_attachList; child; child = child->m_attachNext)
            wielded = wielded || child->m_attachmentId == socket;
        if (!wielded) unit.SheatheHandItem(hand,0);
        unit.m_animationState |= hand ? 0x200000 : 0x100000;
    }
}
inline bool ProfileModel(uint32_t model) { return model >= Profile && model < Profile + 3; }
inline bool Model(uint32_t id) { return id == Bodies[0] || id == Bodies[1] || id == Bodies[2]; }
inline uint32_t ModelForForm(uint32_t sex, uint32_t form) {
    if (sex > 1 || (form && form != Db2::appearance::FormDarkweaver && form != Db2::appearance::FormDreadnaught)) return 0;
    return Profile + (form == Db2::appearance::FormDreadnaught ? 2 : sex);
}
inline std::string Path(uint32_t id) { return "retail/ethereal/" + std::to_string(id) + ".m2"; }
inline uint32_t PathModel(const std::string& path) {
    for (uint32_t id : Bodies) if (path == Path(id)) return id;
    return 0;
}
inline uint32_t OptionCount(uint32_t model) {
    return !ProfileModel(model) ? 0 : model == Profile + 2 ? 8 : uint32_t(std::size(SourceGeosets)) + 11;
}
// Keep installed wire slots stable; the creator presents four exclusive presets over the legacy toggle slots.
inline constexpr uint32_t PresetSlots[4][3] = {{11,12,13},{14,15,16},{29,30,31},{33,34,35}};
inline bool PresetOn(uint32_t slot, uint32_t value) {
    return (value == 0) == (SourceGeosets[slot - 11] % 100 == 1);
}
inline uint32_t PresetValue(const std::vector<uint32_t>& state, uint32_t group) {
    for (uint32_t i = 0; i < 3; ++i) {
        const uint32_t slot = PresetSlots[group][i];
        if (PresetOn(slot, slot < state.size() ? state[slot] : 0)) return i;
    }
    return 0;
}
inline void SetPreset(std::vector<uint32_t>& state, uint32_t group, uint32_t selected) {
    if (group >= 4 || selected >= 3) return;
    state.resize(OptionCount(Profile), 0);
    for (uint32_t i = 0; i < 3; ++i) {
        const uint32_t slot = PresetSlots[group][i];
        state[slot] = uint32_t((i == selected) != (SourceGeosets[slot - 11] % 100 == 1));
    }
    state[32] = 1; // Geoset 501 has no legs and is not an admissible leg preset.
}
inline uint32_t CreatorCount(uint32_t model) { return model == Profile + 2 ? 3 : 6; }
inline uint32_t CreatorSlot(uint32_t model, uint32_t control) {
    static constexpr uint32_t body[] = {0,3,11,14,29,33}, mech[] = {0,1,7};
    return model == Profile + 2 ? mech[control] : body[control];
}
inline const char* CreatorName(uint32_t model, uint32_t control) {
    static const char* body[] = {"Cloth Color","Glow Color","Head","Arms","Chest","Legs"};
    static const char* mech[] = {"Plating Color","Core Color","Lightning Color"};
    return model == Profile + 2 ? mech[control] : body[control];
}
inline uint32_t Option(uint32_t model, uint32_t index) {
    return index < OptionCount(model) ? Profile + 0x100 + (model - Profile) * 0x100 + index : 0;
}
inline bool DecodeOption(uint32_t option, uint32_t& model, uint32_t& index) {
    if (option < Profile + 0x100 || option >= Profile + 0x400) return false;
    const uint32_t local = option - Profile - 0x100;
    model = Profile + local / 0x100; index = local % 0x100;
    return index < OptionCount(model);
}
inline uint32_t Count(uint32_t option) {
    uint32_t model, index;
    if (!DecodeOption(option, model, index)) return 0;
    if (model == Profile + 2) return index == 7 ? EffectCount()
        : index == 0 ? uint32_t(std::size(MechColors)) : index == 1 ? uint32_t(std::size(MechGlowColors)) : 2;
    if (index == 0) return model == Profile ? uint32_t(std::size(MaleCloth)) : uint32_t(std::size(FemaleCloth));
    if (index == 1) return model == Profile ? uint32_t(std::size(MaleBandages)) : uint32_t(std::size(FemaleBandages));
    if (index == 3) return uint32_t(std::size(ExtraColors));
    return index == 2 ? 6 : 2;
}
inline uint32_t FirstChoice(uint32_t model, uint32_t index) {
    return Profile + 0x1000 + (model - Profile) * 0x1000 + index * 64;
}
inline bool Choice(uint32_t option, uint32_t index, Db2::appearance::ChrChoice& out) {
    uint32_t model, ordinal;
    if (!DecodeOption(option, model, ordinal) || index >= Count(option)) return false;
    out = {FirstChoice(model, ordinal) + index, index, index, 0, 0};
    return true;
}
inline bool DecodeChoice(uint32_t choice, uint32_t& model, uint32_t& option, uint32_t& value) {
    if (choice < Profile + 0x1000 || choice >= Profile + 0x4000) return false;
    const uint32_t local = choice - Profile - 0x1000;
    model = Profile + local / 0x1000; option = (local % 0x1000) / 64; value = local % 64;
    return option < OptionCount(model) && value < Count(Option(model, option));
}
inline bool Allowed(uint32_t model, uint32_t choice) {
    uint32_t selected, option, value;
    return DecodeChoice(choice, selected, option, value) && selected == model;
}
inline std::string OptionName(uint32_t option) {
    uint32_t model, index;
    if (!DecodeOption(option, model, index)) return {};
    if (model == Profile + 2) {
        if (index == 7) return "Glow Color";
        if (index < 3) return index == 0 ? "Plating Color" : index == 1 ? "Core Color" : "Ethereal Glow";
        return "Core Layer " + std::to_string(index - 2);
    }
    if (index < 5) {
        static const char* names[] = {"Cloth Color", "Bandage Color", "Energy Color", "Extra Color", "Ethereal Glow"};
        return names[index];
    }
    if (index < 11) return "Core Layer " + std::to_string(index - 4);
    return "Geoset " + std::to_string(SourceGeosets[index - 11]);
}
inline std::string ChoiceName(uint32_t choice) {
    uint32_t model, option, value;
    if (!DecodeChoice(choice, model, option, value)) return {};
    if (model == Profile + 2 && option < 2)
        return value < std::size(MechColorNames) ? MechColorNames[value]
            : CustomColorNames[value - std::size(MechColorNames)];
    if (model == Profile + 2 && option == 7) return Effect(value).name;
    if (model != Profile + 2 && option == 0) {
        const uint32_t originals = uint32_t(model == Profile ? std::size(MaleCloth) : std::size(FemaleCloth)) - CustomColors;
        if (value >= originals) return CustomColorNames[value - originals];
    }
    if (model != Profile + 2 && option == 3 && value >= 2) return CustomColorNames[value - 2];
    if (option >= (model == Profile + 2 ? 2u : 4u)) {
        const bool defaultOn = option < (model == Profile + 2 ? 7u : 11u)
            || (model != Profile + 2 && SourceGeosets[option - 11] % 100 == 1);
        return (value == 0) == defaultOn ? "On" : "Off";
    }
    return "Color " + std::to_string(value + 1);
}
inline bool Glow(const uint32_t* choices, uint32_t count, uint32_t model) {
    const uint32_t first = FirstChoice(model, model == Profile + 2 ? 2 : 4);
    for (uint32_t i = 0; i < count; ++i) if (choices[i] == first + 1) return false;
    return true;
}
template <class ModelType>
inline void ApplyEffects(ModelType& model, const uint32_t* choices, uint32_t count) {
    uint32_t selected = 0;
    const uint32_t first = FirstChoice(Profile + 2, 7);
    for (uint32_t i = 0; i < count; ++i)
        if (choices[i] >= first && choices[i] < first + EffectCount()) selected = choices[i] - first;
    const auto color = [](uint32_t value) {
        return CImVector(uint8_t(value), uint8_t(value >> 8), uint8_t(value >> 16), uint8_t(value >> 24));
    };
    const auto& palette = Effect(selected);
    for (uint32_t i = 0; i < 3; ++i)
        model.ReplaceParticleColor(i + 11, color(palette.start[i]), color(palette.mid[i]), color(palette.end[i]));
}
inline bool Build(uint32_t sex, const uint32_t* choices, uint32_t count, Db2::appearance::Recipe& out,
                  uint32_t form) {
    out.Clear();
    const uint32_t model = ModelForForm(sex, form), options = OptionCount(model);
    if (!model || count > options || (count && !choices)) return false;
    std::array<uint32_t, 64> values{};
    uint64_t seen = 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t selected, option, value;
        if (!DecodeChoice(choices[i], selected, option, value) || selected != model || (seen & (uint64_t{1} << option))) return false;
        seen |= uint64_t{1} << option; values[option] = value;
    }
    const bool mech = model == Profile + 2;
    if (!mech)
    {
        std::vector<uint32_t> presets(values.begin(), values.begin() + options);
        for (uint32_t group = 0; group < 4; ++group) SetPreset(presets, group, PresetValue(presets, group));
        std::copy(presets.begin(), presets.end(), values.begin());
    }
    out.modelFileDataId = Bodies[model - Profile];
    if (!mech) {
        out.textureLayoutId = 141;
        if (!values[5]) out.geosets.push_back(BareHandGeoset);
    }
    const uint32_t first = mech ? 3 : 5, roots = mech ? 4 : 6;
    for (uint32_t i = 0; i < roots; ++i) if (!values[first + i]) out.geosets.push_back(uint16_t(6001 + i));
    if (!mech)
        for (uint32_t i = 0; i < std::size(SourceGeosets); ++i)
            if ((SourceGeosets[i] < 301 || SourceGeosets[i] > 312) &&
                ((values[11 + i] == 0) == (SourceGeosets[i] % 100 == 1))) out.geosets.push_back(uint16_t(6100 + i));
    if (mech) {
        out.layers.push_back({11, 0, 1, UINT32_MAX, 0, MechColors[values[0]]});
        out.layers.push_back({12, 0, 1, UINT32_MAX, 0, MechGlowColors[values[1]]});
    } else {
        out.layers.push_back({11, 0, 1, UINT32_MAX, 0, sex ? FemaleCloth[values[0]] : MaleCloth[values[0]]});
        out.layers.push_back({12, 0, 1, UINT32_MAX, 0, sex ? FemaleBandages[values[1]] : MaleBandages[values[1]]});
        out.layers.push_back({13, 0, 1, UINT32_MAX, 0, EnergyColors[values[2]]});
        out.layers.push_back({5, 0, 1, UINT32_MAX, 0, ExtraColors[values[3]]});
    }
    return true;
}

inline bool PrepareArmorModel(std::vector<uint8_t>& bytes, std::string& error) {
    using namespace SlimeGiant;
    if (!Array(bytes,0x50,16) || !Array(bytes,0x68,2))
    {
        error = "Ethereal armor texture arrays are invalid"; return false;
    }
    const uint32_t count = Word(bytes,0x50);
    if (count > 65533)
    {
        error = "Ethereal armor texture slots are full"; return false;
    }
    std::vector<uint8_t> textures(bytes.begin()+Word(bytes,0x54),bytes.begin()+Word(bytes,0x54)+size_t(count)*16);
    for (uint32_t i = 0; i < count; ++i)
        if (Word(textures,size_t(i)*16) == 14 || Word(textures,size_t(i)*16) == 2)
        {
            error = "Ethereal armor slots already exist"; return false;
        }
    textures.resize(size_t(count+2)*16,0);
    Put(textures,size_t(count)*16,14); Put(textures,size_t(count+1)*16,2);
    auto output = bytes;
    Put(output,0x50,count+2); Put(output,0x54,uint32_t(output.size()));
    output.insert(output.end(),textures.begin(),textures.end());
    std::vector<uint16_t> lookup(std::max(15u,Word(bytes,0x68)),65535);
    for (uint32_t i = 0; i < Word(bytes,0x68); ++i) lookup[i] = Half(bytes,Word(bytes,0x6c)+size_t(i)*2);
    lookup[14] = uint16_t(count); lookup[2] = uint16_t(count+1);
    Put(output,0x68,uint32_t(lookup.size())); Put(output,0x6c,uint32_t(output.size()));
    auto const* first = reinterpret_cast<uint8_t const*>(lookup.data());
    output.insert(output.end(),first,first+lookup.size()*2);
    bytes.swap(output);
    return true;
}

// A private path keeps original NPC consumers unchanged. IDs are stable across LODs and independent of item groups.
inline bool CompactSkin(std::vector<uint8_t>& skin, std::vector<uint8_t>& model, std::string& error) {
    using namespace SlimeGiant;
    if (skin.size() < 48 || model.size() < 0x130 || !Array(skin, 4, 2) || !Array(skin, 20, 4) ||
        !Array(skin, 28, 48) || !Array(model, 0x3c, 48) || !Array(model, 0x78, 2) ||
        Word(skin, 4) != Word(skin, 20) || Word(model, 0x78) > 65535) {
        error = "Invalid Ethereal skin palette arrays"; return false;
    }
    std::vector<uint16_t> pool;
    for (uint32_t i = 0; i < Word(model, 0x78); ++i) pool.push_back(Half(model, Word(model, 0x7c) + size_t(i) * 2));
    auto output = skin;
    std::vector<bool> seen(Word(skin, 4), false);
    for (uint32_t section = 0; section < Word(skin, 28); ++section) {
        const size_t at = Word(skin, 32) + size_t(section) * 48;
        const uint32_t first = Half(skin, at + 4), count = Half(skin, at + 6);
        const uint32_t oldCount = Half(skin, at + 12), oldFirst = Half(skin, at + 14);
        if (first > seen.size() || count > seen.size() - first || !oldCount ||
            oldFirst > pool.size() || oldCount > pool.size() - oldFirst) {
            error = "Invalid Ethereal section palette range"; return false;
        }
        std::set<uint16_t> used;
        for (uint32_t vertex = first; vertex < first + count; ++vertex) {
            if (seen[vertex]) { error = "Ethereal source section vertex windows overlap"; return false; }
            seen[vertex] = true;
            const uint16_t index = Half(skin, Word(skin, 8) + size_t(vertex) * 2);
            if (index >= Word(model, 0x3c)) { error = "Ethereal palette vertex is out of range"; return false; }
            for (uint32_t influence = 0; influence < 4; ++influence) {
                if (!model[Word(model, 0x40) + size_t(index) * 48 + 12 + influence]) continue;
                const uint8_t local = skin[Word(skin, 24) + size_t(vertex) * 4 + influence];
                if (local >= oldCount) { error = "Ethereal local bone is out of range"; return false; }
                const uint16_t bone = pool[oldFirst + local];
                if (bone >= Word(model, 0x2c)) { error = "Ethereal global bone is out of range"; return false; }
                used.insert(bone);
            }
        }
        if (used.empty()) used.insert(pool[oldFirst]);
        if (used.size() > 75) { error = "Ethereal section exceeds the renderer's 75-bone palette"; return false; }
        const std::vector<uint16_t> palette(used.begin(), used.end());
        const auto found = std::search(pool.begin(), pool.end(), palette.begin(), palette.end());
        const size_t start = found == pool.end() ? pool.size() : size_t(found - pool.begin());
        if (found == pool.end()) {
            if (pool.size() + palette.size() > 65535) { error = "Ethereal bone palette table is full"; return false; }
            pool.insert(pool.end(), palette.begin(), palette.end());
        }
        std::map<uint16_t, uint8_t> local;
        for (size_t i = 0; i < palette.size(); ++i) local[palette[i]] = uint8_t(i);
        for (uint32_t vertex = first; vertex < first + count; ++vertex) {
            const uint16_t index = Half(skin, Word(skin, 8) + size_t(vertex) * 2);
            for (uint32_t influence = 0; influence < 4; ++influence) {
                const size_t target = Word(skin, 24) + size_t(vertex) * 4 + influence;
                output[target] = model[Word(model, 0x40) + size_t(index) * 48 + 12 + influence]
                    ? local.at(pool[oldFirst + skin[target]]) : 0;
            }
        }
        const uint16_t paletteCount = uint16_t(palette.size()), paletteStart = uint16_t(start);
        std::memcpy(output.data() + at + 12, &paletteCount, 2);
        std::memcpy(output.data() + at + 14, &paletteStart, 2);
    }
    Put(output, 44, 75);
    if (pool.size() != Word(model, 0x78)) {
        auto prepared = model;
        while (prepared.size() % 4) prepared.push_back(0);
        Put(prepared, 0x78, uint32_t(pool.size())); Put(prepared, 0x7c, uint32_t(prepared.size()));
        const auto* bytes = reinterpret_cast<const uint8_t*>(pool.data());
        prepared.insert(prepared.end(), bytes, bytes + pool.size() * 2);
        model.swap(prepared);
    }
    skin.swap(output);
    return true;
}

inline bool SplitBareHands(std::vector<uint8_t>& skin, std::vector<uint8_t> const& model, std::string& error) {
    using namespace SlimeGiant;
    if (!Array(skin,4,2) || !Array(skin,12,2) || !Array(skin,28,48) || !Array(skin,36,24)
        || !Array(model,0x3c,48) || Word(skin,28)>=65535)
    {
        error = "Ethereal hand isolation arrays are invalid"; return false;
    }
    const uint32_t rows = Word(skin,28);
    std::vector<uint8_t> sections, handRow;
    std::vector<uint16_t> indices, hands;
    for (uint32_t i = 0; i < rows; ++i) {
        const size_t at = Word(skin,32)+size_t(i)*48;
        auto row = std::vector<uint8_t>(skin.begin()+at,skin.begin()+at+48);
        const uint32_t first = uint32_t(Half(skin,at+2))<<16 | Half(skin,at+8), count = Half(skin,at+10);
        if (first>Word(skin,12) || count>Word(skin,12)-first || count%3)
        {
            error = "Ethereal hand isolation triangle range is invalid"; return false;
        }
        const size_t start = indices.size();
        for (uint32_t n = 0; n < count; n+=3) {
            bool hand = Half(skin,at)==6001;
            uint16_t triangle[3];
            for (uint32_t corner = 0; corner < 3; ++corner) {
                const uint16_t lookup = Half(skin,Word(skin,16)+size_t(first+n+corner)*2);
                if (lookup>=Word(skin,4))
                {
                    error = "Ethereal hand lookup is out of range"; return false;
                }
                const uint16_t vertex = Half(skin,Word(skin,8)+size_t(lookup)*2);
                if (vertex>=Word(model,0x3c))
                {
                    error = "Ethereal hand vertex is out of range"; return false;
                }
                float y,z;
                const size_t position = Word(model,0x40)+size_t(vertex)*48;
                std::memcpy(&y,model.data()+position+4,4); std::memcpy(&z,model.data()+position+8,4);
                hand = hand && std::abs(y)>.45f && z>.50f;
                triangle[corner] = lookup;
            }
            auto& output = hand ? hands : indices;
            output.insert(output.end(),std::begin(triangle),std::end(triangle));
        }
        const uint32_t retained = uint32_t(indices.size()-start);
        const uint16_t high = uint16_t(start>>16), low = uint16_t(start), retained16 = uint16_t(retained);
        std::memcpy(row.data()+2,&high,2); std::memcpy(row.data()+8,&low,2);
        std::memcpy(row.data()+10,&retained16,2);
        if (Half(skin,at)==6001) handRow.assign(skin.begin()+at,skin.begin()+at+48);
        sections.insert(sections.end(),row.begin(),row.end());
    }
    if (handRow.empty() || hands.empty() || hands.size()>65535)
    {
        error = "Ethereal source hands could not be isolated"; return false;
    }
    const uint32_t start = uint32_t(indices.size());
    const uint16_t high = uint16_t(start>>16), low = uint16_t(start), count = uint16_t(hands.size());
    std::memcpy(handRow.data(),&BareHandGeoset,2); std::memcpy(handRow.data()+2,&high,2);
    std::memcpy(handRow.data()+8,&low,2); std::memcpy(handRow.data()+10,&count,2);
    sections.insert(sections.end(),handRow.begin(),handRow.end());
    indices.insert(indices.end(),hands.begin(),hands.end());
    std::vector<uint8_t> batches(skin.begin()+Word(skin,40),skin.begin()+Word(skin,40)+size_t(Word(skin,36))*24);
    const uint32_t oldBatches = Word(skin,36);
    for (uint32_t i = 0; i < oldBatches; ++i) {
        const size_t at = Word(skin,40)+size_t(i)*24;
        const uint16_t section = Half(skin,at+4);
        if (section>=rows)
        {
            error = "Ethereal hand batch section is invalid"; return false;
        }
        if (Half(skin,Word(skin,32)+size_t(section)*48)!=6001) continue;
        std::vector<uint8_t> batch(skin.begin()+at,skin.begin()+at+24);
        const uint16_t newSection = uint16_t(rows);
        std::memcpy(batch.data()+4,&newSection,2); std::memcpy(batch.data()+6,&newSection,2);
        batches.insert(batches.end(),batch.begin(),batch.end());
    }
    auto output = skin;
    while (output.size()%4) output.push_back(0);
    Put(output,12,uint32_t(indices.size())); Put(output,16,uint32_t(output.size()));
    auto const* indexBytes = reinterpret_cast<uint8_t const*>(indices.data());
    output.insert(output.end(),indexBytes,indexBytes+indices.size()*2);
    Put(output,28,rows+1); Put(output,32,uint32_t(output.size()));
    output.insert(output.end(),sections.begin(),sections.end());
    Put(output,36,uint32_t(batches.size()/24)); Put(output,40,uint32_t(output.size()));
    output.insert(output.end(),batches.begin(),batches.end());
    skin.swap(output);
    return true;
}

inline bool PrepareSkin(std::vector<uint8_t>& bytes, bool mech, std::string& error,
    std::vector<uint8_t> const* model = nullptr) {
    using namespace SlimeGiant;
    if (bytes.size() < 48 || Word(bytes, 0) != 0x4e494b53 || !Array(bytes, 28, 48)) {
        error = "Ethereal SKIN section layout is invalid"; return false;
    }
    auto output = bytes;
    uint16_t root = 0;
    for (uint32_t i = 0; i < Word(bytes, 28); ++i) {
        const size_t at = Word(bytes, 32) + size_t(i) * 48;
        const uint16_t original = Half(bytes, at);
        uint16_t mapped = 0;
        if (!original) mapped = uint16_t(6001 + root++);
        else {
            const auto found = std::find(std::begin(SourceGeosets), std::end(SourceGeosets), original);
            if (mech || found == std::end(SourceGeosets)) {
                error = "Ethereal source geoset catalog changed"; return false;
            }
            mapped = uint16_t(6100 + (found - std::begin(SourceGeosets)));
        }
        std::memcpy(output.data() + at, &mapped, 2);
    }
    if (root != (mech ? 4 : 6)) { error = "Ethereal core layer count changed"; return false; }
    if (!mech && model && !SplitBareHands(output,*model,error)) return false;
    bytes.swap(output);
    return true;
}
}
#endif
