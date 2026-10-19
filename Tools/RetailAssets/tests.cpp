#include "Engine/Source/Retail/CascSource.h"
#include "Engine/Source/Retail/RetailModel.h"
#include "Engine/Source/Retail/ClientTables.h"
#include "Engine/Source/Retail/RetailShader.h"
#include "Engine/Source/Retail/Equipment.h"
#include "Engine/Source/Retail/Compose.h"
#include "Engine/Source/Retail/RaceGeometry.h"
#include "Engine/Source/Retail/RacePolicy.h"
#include "Engine/Source/Retail/LegacyAppearance.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {
void Check(bool pass, const char* message) {
    if (!pass) { std::fprintf(stderr, "FAILED: %s\n", message); std::exit(1); }
}
void Put(std::vector<uint8_t>& b, size_t p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) b[p + i] = uint8_t(v >> (i * 8));
}
uint32_t Get(const std::vector<uint8_t>& b, size_t p) {
    return uint32_t(b[p]) | uint32_t(b[p + 1]) << 8 | uint32_t(b[p + 2]) << 16 | uint32_t(b[p + 3]) << 24;
}
void Chunk(std::vector<uint8_t>& b, const char* tag, const std::vector<uint8_t>& data) {
    const size_t p = b.size();
    b.resize(p + 8);
    for (size_t i = 0; i < 4; ++i) b[p + i] = tag[i];
    Put(b, p + 4, uint32_t(data.size()));
    b.insert(b.end(), data.begin(), data.end());
}
std::vector<uint8_t> Word(uint32_t v) { std::vector<uint8_t> b(4); Put(b, 0, v); return b; }

// A small valid wire model with one bone, sequence, vertex, texture and material.
// Nonzero offsets exercise MD21's payload-relative addressing and appended TXID paths.
std::vector<uint8_t> Body() {
    std::vector<uint8_t> b(312);
    Put(b, 0, 0x3032444d); Put(b, 4, 274); Put(b, 0x44, 1);
    auto array = [&](size_t field, uint32_t count, size_t stride) {
        size_t offset = b.size(); Put(b, field, count); Put(b, field + 4, uint32_t(offset)); b.resize(offset + count * stride); return offset;
    };
    size_t seq = array(0x1c, 1, 64); Put(b, seq + 12, 0x20); Put(b, seq + 28, 150 | (250 << 16));
    size_t bone = array(0x2c, 1, 88); b[bone + 8] = b[bone + 9] = 0xff;
    for (size_t p : {bone + 16, bone + 36, bone + 56}) b[p + 2] = b[p + 3] = 0xff;
    size_t vertex = array(0x3c, 1, 48); b[vertex + 12] = 255;
    array(0x50, 1, 16);
    array(0x70, 1, 4);
    array(0x78, 1, 2);
    array(0x80, 1, 2);
    array(0x88, 1, 2);
    size_t weight = array(0x90, 1, 2); b[weight] = b[weight + 1] = 0xff;
    size_t transform = array(0x98, 1, 2); b[transform] = b[transform + 1] = 0xff;
    return b;
}
std::vector<uint8_t> Model(const std::vector<uint8_t>& body) {
    std::vector<uint8_t> b;
    Chunk(b, "MD21", body); Chunk(b, "SFID", Word(123)); Chunk(b, "TXID", Word(456));
    return b;
}
std::vector<uint8_t> Skin() {
    std::vector<uint8_t> b(48);
    Put(b, 0, 0x4e494b53); Put(b, 44, 1);
    auto array = [&](size_t field, uint32_t count, size_t stride) {
        size_t offset = b.size(); Put(b, field, count); Put(b, field + 4, uint32_t(offset)); b.resize(offset + count * stride); return offset;
    };
    array(4, 1, 2); array(12, 3, 2); array(20, 1, 4);
    size_t section = array(28, 1, 48);
    b[section + 6] = 1; b[section + 10] = 3; b[section + 12] = 1; b[section + 16] = 1;
    size_t batch = array(36, 1, 24);
    b[batch + 8] = b[batch + 9] = 0xff; b[batch + 14] = 1;
    return b;
}
}

int main(int argc, char** argv) {
    {
        Retail::Image skin{3, 1, {100, 120, 140, 255, 100, 120, 140, 255, 100, 120, 140, 255}};
        const Retail::Image tattoo{3, 1, {255, 255, 255, 255, 128, 128, 128, 255, 0, 0, 0, 0}};
        Retail::PaintImage(skin, tattoo, 0, 0, 3, 1, 4); // Orc layout 105, target 16: multiply.
        Check(skin.rgba == std::vector<uint8_t>{100, 120, 140, 255, 50, 60, 70, 255, 100, 120, 140, 255},
            "Orc tattoo white background and transparent texels preserve skin; only dark marks shade it");

        auto painted = [](std::vector<uint8_t> base, std::vector<uint8_t> source, uint32_t mode) {
            Retail::Image dst{1, 1, std::move(base)}, src{1, 1, std::move(source)};
            Retail::PaintImage(dst, src, 0, 0, 1, 1, mode);
            return dst.rgba;
        };
        // Golden pixels from the pinned character shader and its framebuffer blend factors,
        // including translucent destination alpha rather than only opaque body sheets.
        Check(painted({80, 120, 160, 128}, {128, 64, 255, 128}, 4) == std::vector<uint8_t>{70, 97, 160, 112},
            "DB2 multiply uses shader-result alpha before framebuffer blending");
        Check(painted({64, 128, 192, 255}, {64, 192, 128, 255}, 6) == std::vector<uint8_t>{32, 192, 192, 255},
            "DB2 overlay uses source thresholds instead of Mod2x");
        Check(painted({64, 128, 192, 255}, {64, 192, 128, 255}, 7) == std::vector<uint8_t>{112, 224, 224, 255},
            "DB2 screen preserves the authored highlight operation");
        Check(painted({100, 150, 200, 64}, {200, 100, 50, 128}, 9) == std::vector<uint8_t>{150, 125, 125, 160},
            "DB2 straight alpha blends RGB and unions destination alpha");
        Check(painted({100, 150, 200, 64}, {200, 100, 50, 128}, 15) == std::vector<uint8_t>{150, 125, 125, 96},
            "DB2 inferred alpha uses source-alpha factors for alpha too");
        for (uint32_t mode : {0u, 1u})
            Check(painted({100, 150, 200, 255}, {4, 5, 6, 0}, mode) == std::vector<uint8_t>{4, 5, 6, 0},
                "DB2 base blit retains authored RGB beneath a transparent mask");
        Check(painted({100, 150, 200, 64}, {200, 100, 50, 128}, 2) == std::vector<uint8_t>{180, 110, 80, 160},
            "legacy equipment mode 2 retains straight alpha-over on translucent skin");
        Retail::Image armor{2, 1, {100, 120, 140, 255, 100, 120, 140, 255}};
        const Retail::Image item{1, 1, {200, 40, 60, 128}};
        Retail::PaintImage(armor, item, 1, 0, 1, 1, 2);
        Check(armor.rgba == std::vector<uint8_t>{100, 120, 140, 255, 150, 80, 100, 255},
            "legacy armor still paints only its equipment rectangle over the Human body");
    }
    {
        using namespace Retail::LegacyAppearance;
        for (const auto& option : Options) {
            std::array<uint8_t, 13> fields{};
            // Populate neighboring digits too: modifying one retail option must preserve every
            // other saved choice sharing its original byte, including extra fields above five.
            for (const auto& neighbor : Options) if (neighbor.race == option.race && neighbor.sex == option.sex)
                Check(Encode(neighbor.race, neighbor.sex, neighbor.id, Choices[neighbor.offset + neighbor.count - 1], fields),
                    "installed packed codec fits all authored choices");
            for (uint32_t i = 0; i < option.count; ++i) {
                const auto before = fields;
                const uint32_t selected = Choices[option.offset + i];
                uint32_t decoded = 0;
                Check(Encode(option.race, option.sex, option.id, selected, fields)
                    && Decode(option.race, option.sex, option.id, fields, decoded) && decoded == selected,
                    "frozen source choice IDs survive Retail option reordering");
                for (const auto& neighbor : Options) if (neighbor.race == option.race && neighbor.sex == option.sex && neighbor.id != option.id) {
                    uint32_t oldChoice = 0, newChoice = 0;
                    Check(Decode(neighbor.race, neighbor.sex, neighbor.id, before, oldChoice)
                        && Decode(neighbor.race, neighbor.sex, neighbor.id, fields, newChoice) && oldChoice == newChoice,
                        "changing a packed selector preserves all neighboring saved choices");
                }
            }
            const auto before = fields;
            Check(!Encode(option.race, option.sex, option.id, UINT32_MAX, fields) && fields == before,
                "new unencodable Retail choices cannot corrupt installed appearance bytes");
        }
        Check(Retail::HelmetMaskRace(85) == 3 && Retail::HelmetMaskRace(91) == 4,
            "Horde Earthen/Haranir helmets use visual anatomy rather than faction eligibility");
        Check(!Retail::UsesLegacyClosedFeet(6) && !Retail::UsesLegacyClosedFeet(11)
            && !Retail::UsesLegacyClosedFeet(86) && Retail::UsesLegacyClosedFeet(3),
            "legacy boots preserve hooves and authored Feet choices");
    }
    {
        struct FootModel {
            bool visible[100];
            FootModel() { std::fill(std::begin(visible), std::end(visible), true); }
            void SetGeometryVisible(uint32_t first, uint32_t last, int visibleValue) {
                for (uint32_t id = first; id <= last; ++id) visible[id - 2000] = visibleValue != 0;
            }
            bool Only(uint16_t id) const {
                for (uint16_t i = 1; i < 100; ++i) if (visible[i] != (i + 2000 == id)) return false;
                return true;
            }
        } model;
        Retail::ApplyLegacyFootGeometry(model, 10141, true); // Actual style-zero starter boots.
        Check(model.Only(2002), "style-zero starter boots hide bare toes and all alternate foot meshes");
        Retail::ApplyLegacyFootGeometry(model, 64789, true);
        Check(model.Only(2002), "swapping to styled boots keeps the closed shoe independently of shin style");
        Retail::ApplyLegacyFootGeometry(model, 64789, true); // A robe refresh retains equipped boots.
        Check(model.Only(2002), "a geometry refresh does not restore bare toes under equipped boots");
        Retail::ApplyLegacyFootGeometry(model, 0, true);
        Check(model.Only(2001), "removing boots hides the shoe and restores the original bare foot");
        Retail::ApplyLegacyFootGeometry(model, 10141, true);
        Check(model.Only(2002), "re-equipping boots restores the closed shoe without leftover bare feet");
        Retail::ApplyLegacyFootGeometry(model, 10141, false);
        Check(model.Only(2001), "models lacking a closed-foot mesh retain a visible foot fallback");
    }
    {
        // Real pinned Dwarf recipes omit group 32; the shared visibility reset used to hide both heads.
        const std::vector<uint16_t> dwarfMale{0, 300, 102, 3500, 1600, 3401};
        const std::vector<uint16_t> dwarfFemale{2, 3500, 1600};
        auto has = [](const std::vector<uint16_t>& ids, uint16_t id) {
            return std::find(ids.begin(), ids.end(), id) != ids.end();
        };
        for (const auto& recipe : {dwarfMale, dwarfFemale}) {
            const auto defaults = Retail::DefaultRaceGeosets(recipe);
            Check(has(defaults, 3202) && !has(defaults, 3201),
                "Dwarf recipe restores the full face/head instead of the neck-only variant");
            Check(has(defaults, 2001) && has(defaults, 2201) && has(defaults, 3301) && has(defaults, 5101),
                "head fallback preserves the existing feet, torso and eyes");
        }
        for (const uint16_t shape : {3200, 3202, 3203, 3204}) {
            const auto defaults = Retail::DefaultRaceGeosets({0, 300, 100, 200, shape, 3400, 702});
            Check(!has(defaults, 3202), "explicit Human face shapes and hidden-head choices suppress the fallback");
        }
        Check(!has(Retail::DefaultRaceGeosets({2004, 2202, 3302, 5103}), 2001),
            "anatomy defaults do not override selected equipment/body variants");
        // Pinned source skins 974391/921897, 1100099/1100298 and 1734086/1733813
        // have palms/fingers in 2301, separate from every 401..404 glove cuff. Their
        // default customization recipes omit group 23 for both sexes.
        const std::vector<std::vector<uint16_t>> elfRecipes{
            {2, 200, 300, 100, 3400, 701, 3900, 5100},
            {2, 3900, 3401, 701, 3700, 3500, 1600, 3600, 5100},
            {2, 100, 702, 5100}, {2, 702, 3500, 3600, 3900, 5100},
            {2, 100, 702, 200, 5100}, {2, 3500, 702, 200, 5100}
        };
        for (const auto& recipe : elfRecipes) {
            for (const uint16_t glove : {401, 402, 403, 404}) {
                auto equipped = recipe;
                equipped.push_back(glove);
                Check(has(Retail::DefaultRaceGeosets(equipped), 2301),
                    "both sexes of Night/Blood/Void Elves retain hands with every legacy glove style");
            }
        }
        for (const uint16_t selectedHand : {2300, 2301, 2302})
            Check(!has(Retail::DefaultRaceGeosets({selectedHand}), 2301),
                "explicit hidden or alternate hand recipes suppress the hand fallback");
    }
    // Reproduce world rendering: item identity survives temporary texture cleanup, and an update
    // of one section must retain the other equipped sections in the next complete retail sheet.
    struct Section {
        void* m_layerTex[7]{};
        uint32_t m_layerItemDisplayId[7]{};
        uint32_t m_layerMask = 0;
    } equipment[8];
    int loadedTexture = 0;
    equipment[3].m_layerItemDisplayId[1] = 100; // chest
    equipment[3].m_layerMask = 2;
    equipment[3].m_layerTex[1] = &loadedTexture;
    equipment[7].m_layerItemDisplayId[0] = 200; // boots
    equipment[7].m_layerMask = 1;
    equipment[7].m_layerTex[0] = &loadedTexture;
    auto beforeCleanup = Retail::CollectEquipmentLayers(equipment, false);
    equipment[3].m_layerTex[1] = equipment[7].m_layerTex[0] = nullptr;
    auto afterCleanup = Retail::CollectEquipmentLayers(equipment, false);
    Check(beforeCleanup.size() == 2 && afterCleanup.size() == 2
        && afterCleanup[0].displayId == 100 && afterCleanup[1].displayId == 200,
        "temporary texture cleanup preserves the equipped body layers");
    equipment[7].m_layerItemDisplayId[0] = 201;
    equipment[7].m_layerTex[0] = &loadedTexture;
    auto partialUpdate = Retail::CollectEquipmentLayers(equipment, false);
    Check(partialUpdate.size() == 2 && partialUpdate[0].displayId == 100
        && !partialUpdate[0].texture && partialUpdate[1].displayId == 201,
        "changing boots retains the chest even when its texture handle was released");
    equipment[7] = {};
    auto removedBoots = Retail::CollectEquipmentLayers(equipment, false);
    Check(removedBoots.size() == 1 && removedBoots[0].displayId == 100,
        "unequipping boots removes only their body layer");
    equipment[5].m_layerItemDisplayId[0] = 300; // pants hidden below robe
    equipment[5].m_layerMask = 1;
    auto robeLayers = Retail::CollectEquipmentLayers(equipment, true);
    auto pantsLayers = Retail::CollectEquipmentLayers(equipment, false);
    Check(robeLayers.size() == 1 && pantsLayers.size() == 2,
        "robe removal restores trousers without changing saved equipment");
    {
        // A legacy helmet is authored relative to the crown socket, while the retail socket is
        // at the head pivot. Test a different head position on all axes rather than a fixed lift.
        const uint16_t dwarfSocket[] = {32767, 32767, 32767, 65535};
        const uint16_t oppositeIdentity[] = {32768, 32768, 32768, 0};
        const uint16_t rotatedSocket[] = {32767, 55938, 32767, 55938};
        const uint16_t invalidSocket[] = {32767, 32767, 32767, 32767};
        Check(Retail::IsIdentityHelmetRotation(dwarfSocket) && Retail::IsIdentityHelmetRotation(oppositeIdentity),
            "accept quantized global identity socket rotations on Dwarf helmets");
        Check(!Retail::IsIdentityHelmetRotation(rotatedSocket) && !Retail::IsIdentityHelmetRotation(invalidSocket),
            "do not apply a translation-only helmet fit to a rotated or invalid socket");
        auto donor = Body();
        Put(donor, 4, 264);
        auto array = [&](size_t field, uint32_t count, size_t stride) {
            const size_t offset = donor.size();
            Put(donor, field, count); Put(donor, field + 4, uint32_t(offset));
            donor.resize(offset + count * stride);
            return offset;
        };
        auto point = [&](size_t offset, float x, float y, float z) {
            const float xyz[] = {x, y, z};
            for (size_t i = 0; i < 3; ++i) {
                uint32_t bits;
                std::memcpy(&bits, &xyz[i], 4);
                Put(donor, offset + i * 4, bits);
            }
        };
        const size_t keys = array(0x34, 7, 2);
        const size_t attachments = array(0xf0, 1, 40);
        const size_t lookup = array(0xf8, 12, 2);
        point(Get(donor, 0x30) + 76, 1, 2, 3);
        Put(donor, attachments, 11);
        point(attachments + 8, 2, 4, 6);
        Retail::HelmetAnchor anchor;
        std::string anchorError;
        Check(Retail::ReadHelmetAnchor(donor, anchor, anchorError), "read installed legacy helmet anchor");
        const auto original = donor;
        const Retail::HelmetAnchor target{{10, 20, 30}, {9, 18, 27}};
        const auto correction = Retail::HelmetTranslation(anchor, target);
        Check(correction == std::array<float, 3>{2, 4, 6}, "retarget crown origin to a different retail head/socket");
        Check(donor == original, "helmet fitting never changes source model bytes");
        Check(Retail::HelmetTranslation(anchor, anchor) == std::array<float, 3>{0, 0, 0},
            "matching skeletons retain the original helmet placement");
        donor[lookup + 22] = donor[lookup + 23] = 0xff;
        Check(!Retail::ReadHelmetAnchor(donor, anchor, anchorError), "reject a missing helmet socket");
        donor = original; donor[keys + 12] = donor[keys + 13] = 0xff;
        Check(!Retail::ReadHelmetAnchor(donor, anchor, anchorError), "reject a missing head key bone");
        donor = original; Put(donor, attachments + 8, 0x7fc00000);
        Check(!Retail::ReadHelmetAnchor(donor, anchor, anchorError), "reject a nonfinite helmet origin");
    }
    std::string error;
    Retail::ModelFiles files;
    std::vector<uint8_t> prepared;
    {
        auto largeRig = Body();
        const size_t originalBone = Get(largeRig, 0x30), bones = largeRig.size();
        const std::vector<uint8_t> bind(largeRig.begin() + originalBone, largeRig.begin() + originalBone + 88);
        for (uint32_t i = 0; i < 300; ++i) largeRig.insert(largeRig.end(), bind.begin(), bind.end());
        Put(largeRig, 0x2c, 300); Put(largeRig, 0x30, uint32_t(bones));
        const size_t palette = Get(largeRig, 0x7c);
        largeRig[palette] = uint8_t(299); largeRig[palette + 1] = uint8_t(299 >> 8);
        Check(Retail::PrepareModel(Model(largeRig), prepared, files, error),
            "large character rigs retain uint16 global bone indices beyond 255");
        Check(Retail::ValidateSkin(Skin(), prepared, error),
            "byte section-local palette can reference global character bone 299");
        Put(largeRig, 0x2c, 65536);
        Check(!Retail::PrepareModel(Model(largeRig), prepared, files, error),
            "reject rigs exceeding the global uint16 bone domain");
    }
    {
        auto skeleton = [](bool parent, bool conflict = false) {
            std::vector<uint8_t> sks(24, 0), skb(16 + 88, 0), result;
            const uint32_t count = parent ? (conflict ? 3 : 2) : 1;
            const size_t sequences = sks.size(); sks.resize(sequences + count * 64);
            Put(sks, 8, count); Put(sks, 12, uint32_t(sequences));
            for (uint32_t i = 0; i < count; ++i) {
                const size_t p = sequences + size_t(i) * 64;
                Put(sks, p, parent ? (i == 2 ? 69 : i ? 60 : 0) : conflict ? 60 : 5);
                Put(sks, p + 4, 1000); Put(sks, p + 12, (parent && i) || (!parent && conflict) ? 0 : 0x20);
                Put(sks, p + 60, 0xffffffff);
                if (parent && i == 2) { Put(sks, p + 12, 0x40); sks[p + 62] = 1; sks[p + 63] = 0; }
            }
            const uint32_t lookupCount = parent ? (conflict ? 70 : 61) : conflict ? 61 : 6;
            const size_t lookup = sks.size(); sks.resize(lookup + lookupCount * 2, 0xff);
            Put(sks, 16, lookupCount); Put(sks, 20, uint32_t(lookup));
            const size_t primary = lookup + (parent ? 0 : conflict ? 120 : 10);
            sks[primary] = sks[primary + 1] = 0;
            if (parent) { sks[lookup + 120] = 1; sks[lookup + 121] = 0; }
            Put(skb, 0, 1); Put(skb, 4, 16); Put(skb, 16 + 8, 0xffff);
            for (const size_t track : {size_t(32), size_t(52), size_t(72)}) skb[track + 2] = skb[track + 3] = 0xff;
            if (parent) {
                const size_t times = skb.size(); skb.resize(times + 16);
                const size_t values = skb.size(); skb.resize(values + 16);
                const size_t residentTime = skb.size(); skb.resize(residentTime + 4);
                const size_t residentValue = skb.size(); skb.resize(residentValue + 12);
                Put(skb, 36, 2); Put(skb, 40, uint32_t(times));
                Put(skb, 44, 2); Put(skb, 48, uint32_t(values));
                Put(skb, times, 1); Put(skb, times + 4, uint32_t(residentTime));
                Put(skb, values, 1); Put(skb, values + 4, uint32_t(residentValue));
                Put(skb, times + 8, 2); Put(skb, times + 12, 0);
                Put(skb, values + 8, 2); Put(skb, values + 12, 8);
            }
            Chunk(result, "SKS1", sks); Chunk(result, "SKB1", skb);
            if (parent || conflict) { std::vector<uint8_t> afid(8); Put(afid, 0, 60); Put(afid, 4, parent ? 777 : 888); Chunk(result, "AFID", afid); }
            return result;
        };
        const auto child = skeleton(false), parent = skeleton(true);
        std::vector<uint8_t> merged;
        Check(Retail::MergeSkeleton(child, parent, merged, error), error.c_str());
        auto rawBody = Body(); Put(rawBody, 0x1c, 0); Put(rawBody, 0x2c, 0);
        auto model = Model(rawBody); Chunk(model, "SKID", Word(111));
        Check(Retail::PrepareModel(model, prepared, files, error, &merged), error.c_str());
        Check(Get(prepared, 0x1c) == 3 && (Get(prepared, Get(prepared, 0x20)) & 65535) == 5,
            "parent skeleton adds missing actions without replacing the child's authored sequence");
        std::vector<uint8_t> payload(32, 0), animation;
        Put(payload, 4, 100); Put(payload, 20, 0x3f800000); Chunk(animation, "AFSB", payload);
        Check(Retail::ValidateAnimation(animation, prepared, 60, 0, error), error.c_str());
        Check(files.animations.size() == 1 && files.animations[0].fileId == 777,
            "inherited external motion retains the parent's original AFID payload owner");
        Check(child == skeleton(false) && parent == skeleton(true), "flattening skeleton inheritance preserves source bytes");
        Check(Retail::MergeSkeleton(skeleton(false, true), skeleton(true, true), merged, error), error.c_str());
        Check(Retail::PrepareModel(model, prepared, files, error, &merged), error.c_str());
        Check(Retail::ValidateAnimation(animation, prepared, 69, 0, error),
            "inherited aliases use their parent's motion when the child overrides the aliased action");
        Check(std::any_of(files.animations.begin(), files.animations.end(), [](const Retail::AnimationFile& a) { return a.id == 69 && a.fileId == 777; })
            && std::any_of(files.animations.begin(), files.animations.end(), [](const Retail::AnimationFile& a) { return a.id == 60 && a.fileId == 888; }),
            "parent aliases and overridden child actions retain separate animation payload owners");
        auto corruptParent = parent;
        const size_t boneChunk = 8 + Get(parent, 4) + 8;
        Put(corruptParent, boneChunk + 16 + 12, 123);
        Check(!Retail::MergeSkeleton(child, corruptParent, merged, error), "reject incompatible inherited bone identities");
    }
    auto table = [](uint32_t fields, uint32_t id) {
        std::vector<uint8_t> data(20 + fields * 4 + 1);
        data[0] = 'W'; data[1] = 'D'; data[2] = 'B'; data[3] = 'C';
        Put(data, 4, 1); Put(data, 8, fields); Put(data, 12, fields * 4); Put(data, 16, 1); Put(data, 20, id);
        return data;
    };
    const auto baseItem = table(8, 10), newItem = table(8, 20);
    Check(Retail::MergeClientTable("item.dbc", baseItem, newItem, prepared, error) && Get(prepared, 4) == 2, "append new item records");
    Check(!Retail::MergeClientTable("item.dbc", baseItem, baseItem, prepared, error), "never overwrite existing client IDs");
    auto baseDisplay = table(25, 10), newDisplay = table(25, 20);
    baseDisplay.push_back('A'); baseDisplay.push_back(0); Put(baseDisplay, 16, 3); Put(baseDisplay, 24, 1);
    newDisplay.push_back('B'); newDisplay.push_back(0); Put(newDisplay, 16, 3); Put(newDisplay, 24, 1);
    Check(Retail::MergeClientTable("itemdisplayinfo.dbc", baseDisplay, newDisplay, prepared, error), "append display metadata with private strings");
    Check(Get(prepared, 24) == 1 && Get(prepared, 124) == 4 && prepared[224] == 'B', "preserve original string offsets and relocate additions");
    auto historicalDisplay = baseDisplay; Put(historicalDisplay, 32, 0xffffffff);
    Check(Retail::MergeClientTable("itemdisplayinfo.dbc", historicalDisplay, newDisplay, prepared, error) &&
        Get(prepared, 32) == 0xffffffff, "preserve opaque historical metadata without modifying unrelated rows");
    auto invalidTable = newDisplay; Put(invalidTable, 24, 999);
    Check(!Retail::MergeClientTable("itemdisplayinfo.dbc", baseDisplay, invalidTable, prepared, error), "reject invalid delta strings");
    invalidTable = newItem; Put(invalidTable, 4, 0xffffffff);
    Check(!Retail::MergeClientTable("item.dbc", baseItem, invalidTable, prepared, error), "reject overflowing table counts");
    for (const auto& schema : {std::pair<const char*, unsigned>{"creaturemodeldata.dbc", 28},
        {"creaturedisplayinfo.dbc", 16}, {"spell.dbc", 234}, {"spellicon.dbc", 2}, {"skilllineability.dbc", 14}}) {
        Check(Retail::MergeClientTable(schema.first, table(schema.second, 1), table(schema.second, 2), prepared, error), "mount table additions");
        Check(!Retail::MergeClientTable(schema.first, table(schema.second, 1), table(schema.second, 1), prepared, error), "mount ID collision rejection");
    }
    Check(Retail::MergeClientTable("spell.dbc", table(234, 13990005), table(234, 300000), prepared, error) &&
        Get(prepared, 20) == 13990005, "preserve Esteria's existing high spell IDs verbatim");
    Check(!Retail::MergeClientTable("spell.dbc", table(234, 1), table(234, 13990005), prepared, error), "new imports retain bounded allocation policy");
    const auto body = Body(), valid = Model(body);
    Check(Retail::PrepareModel(valid, prepared, files, error), error.c_str());
    Check(Get(prepared, 4) == 264 && files.version == 274, "validated in-memory layout");
    Check(Get(prepared, Get(prepared, 0x20) + 28) == 150, "Retail split blend durations adapted");
    size_t texture = Get(prepared, 0x54);
    const auto path = reinterpret_cast<const char*>(prepared.data() + Get(prepared, texture + 12));
    Check(std::string(path) == "retail/456.blp", "TXID virtual filename");
    Check(files.skins.size() == 1 && files.skins[0] == 123, "SFID resolution");
    Check(Get(prepared, 0x58) == 1 && prepared[Get(prepared, 0x94)] == 0, "constant-one opacity channel replaces Retail sentinel");
    Check(Get(valid, 12) == 274, "original bytes unchanged");
    auto chunked = body; Put(chunked, 16, 0x200000);
    Check(Retail::PrepareModel(Model(chunked), prepared, files, error), "embedded animation with chunked-file capability flag");
    Put(chunked, Get(chunked, 0x20) + 12, 0);
    Check(!Retail::PrepareModel(Model(chunked), prepared, files, error), "do not accept unsupported chunked external animations");
    Check(Retail::PrepareModel(valid, prepared, files, error), "reset after animation checks");
    const auto skin = Skin();
    Check(Retail::ValidateSkin(skin, prepared, error), error.c_str());
    auto zeroMax = skin; Put(zeroMax, 44, 0);
    Check(Retail::ValidateSkin(zeroMax, prepared, error), "Retail skin with unspecified instance palette maximum");
    auto unusedInfluences = zeroMax; unusedInfluences[Get(zeroMax, 32) + 16] = 0;
    Check(Retail::PrepareSkin(unusedInfluences, prepared, error), "derive unused Retail skin influence count");
    Check(unusedInfluences[Get(zeroMax, 32) + 16] == 1, "reconstructed skin influence count");
    for (size_t n = 0; n < valid.size(); ++n) {
        std::vector<uint8_t> truncated(valid.begin(), valid.begin() + n);
        Check(!Retail::PrepareModel(truncated, prepared, files, error), "reject truncated model at every byte boundary");
    }
    auto bad = valid;
    Put(bad, 4, 0xffffffff); Check(!Retail::PrepareModel(bad, prepared, files, error), "reject oversized chunk");
    bad = valid; Chunk(bad, "MD21", body); Check(!Retail::PrepareModel(bad, prepared, files, error), "reject duplicate model chunk");
    bad = valid; Chunk(bad, "SKID", Word(111));
    Check(!Retail::PrepareModel(bad, prepared, files, error) && error.find("SKEL") != std::string::npos, "explicit shared skeleton rejection");
    auto corruptBody = body;
    Put(corruptBody, 0x3c, 0xffffffff);
    Check(!Retail::PrepareModel(Model(corruptBody), prepared, files, error), "reject overflowing array count");
    corruptBody = body; Put(corruptBody, 0x128, 1);
    Check(Retail::PrepareModel(Model(corruptBody), prepared, files, error) && Get(prepared, 0x128) == 0, "omit unsupported particles");
    {
        std::vector<uint8_t> chunked = {'A', 'F', 'M', '2', 3, 0, 0, 0, 7, 8, 9}, legacy = {7, 8, 9};
        std::string chunkError;
        Check(Retail::StripAnimationChunk(chunked, chunkError) && chunked == legacy, "strip AFM2 animation chunk header");
        Check(Retail::StripAnimationChunk(legacy, chunkError) && legacy.size() == 3, "leave legacy animation files alone");
        std::vector<uint8_t> truncated = {'A', 'F', 'M', '2', 9, 0, 0, 0, 7};
        Check(!Retail::StripAnimationChunk(truncated, chunkError), "reject truncated AFM2 chunk");
    }
    {
        // Same external offsets intentionally address different values in each payload. The old
        // adapter either discarded AFSB after AFM2 or read an AFSB chunk header as timestamps.
        auto owner = Body();
        Put(owner, Get(owner, 0x20) + 12, 0); // sequence zero is external
        auto array = [&](size_t field, uint32_t count, size_t stride) {
            const size_t offset = owner.size();
            Put(owner, field, count); Put(owner, field + 4, uint32_t(offset));
            owner.resize(offset + count * stride);
            return offset;
        };
        auto track = [&](size_t at) {
            Put(owner, at, 0xffff0001); // linear, no global sequence
            size_t times = array(at + 4, 1, 8);
            Put(owner, times, 2); Put(owner, times + 4, 0);
            size_t keys = array(at + 12, 1, 8);
            Put(owner, keys, 2); Put(owner, keys + 4, 8);
        };
        track(Get(owner, 0x30) + 16);
        size_t weight = array(0x58, 1, 20);
        track(weight);
        size_t attachment = array(0xf0, 1, 40);
        Put(owner, attachment, 11);
        track(attachment + 20);
        std::vector<uint8_t> model(12), bones(32), attachments(10), split;
        for (auto* payload : {&model, &bones, &attachments}) Put(*payload, 4, 100);
        Put(model, 8, 32767);
        Put(bones, 20, 0x3f800000); // translation x changes from zero to one
        attachments[8] = 1;
        Chunk(split, "AFM2", model); Chunk(split, "AFSB", bones); Chunk(split, "AFSA", attachments);
        const auto original = split;
        Check(Retail::StripAnimationChunk(split, error) && split == original,
            "retain every payload in a shared-skeleton animation");
        Retail::AnimationLayout layout;
        Check(Retail::InspectAnimation(split.data(), split.size(), layout, error)
            && layout.chunked && layout.model.size == 12 && layout.bones.size == 32
            && layout.attachments.size == 10 && layout.model.offset != layout.bones.offset,
            "separate model, skeleton-bone and skeleton-attachment address spaces");
        Check(Retail::ValidateAnimation(split, owner, 0, 0, error), error.c_str());
        auto missingBones = std::vector<uint8_t>{};
        Chunk(missingBones, "AFM2", model);
        Check(!Retail::ValidateAnimation(missingBones, owner, 0, 0, error),
            "do not validate skeletal keyframes against the short model payload");
        auto corrupt = split; Put(corrupt, layout.bones.offset, 101);
        Check(!Retail::ValidateAnimation(corrupt, owner, 0, 0, error), "check skeletal timestamp ordering inside AFSB");
        corrupt = split; corrupt.pop_back();
        Check(!Retail::InspectAnimation(corrupt.data(), corrupt.size(), layout, error), "reject truncated skeleton attachment chunks");
        corrupt = split; Chunk(corrupt, "AFSB", bones);
        Check(!Retail::InspectAnimation(corrupt.data(), corrupt.size(), layout, error), "reject ambiguous duplicate skeleton payloads");
        Put(owner, 0x58, 0); Put(owner, 0xf0, 0); // body needs only skeletal data
        std::vector<uint8_t> boneOnly; Chunk(boneOnly, "AFSB", bones);
        Check(Retail::ValidateAnimation(boneOnly, owner, 0, 0, error), "support AFSB-only talk/laugh files");
    }
    corruptBody = body; Put(corruptBody, Get(body, 0x30) + 8, 0);
    Check(!Retail::PrepareModel(Model(corruptBody), prepared, files, error), "reject bone parent cycle");
    corruptBody = body;
    size_t bone = Get(body, 0x30);
    Put(corruptBody, bone + 20, 1); Put(corruptBody, bone + 24, 0xfffffff0);
    Check(!Retail::PrepareModel(Model(corruptBody), prepared, files, error), "reject malformed track array");
    Check(Retail::PrepareModel(valid, prepared, files, error), "reset prepared model");
    bad = skin; Put(bad, 8, 0xfffffff0);
    Check(!Retail::ValidateSkin(bad, prepared, error), "reject bad skin offset");
    bad = skin; bad[Get(skin, 40) + 14] = 3;
    Check(!Retail::ValidateSkin(bad, prepared, error), "reject unsupported multitexture batch");
    bad = skin; bad[Get(skin, 40) + 2] = 20; bad[Get(skin, 40) + 3] = 0x80;
    Check(!Retail::ValidateSkin(bad, prepared, error), "reject unsupported shader");
    bad = skin; bad[Get(skin, 24)] = 5;
    Check(!Retail::ValidateSkin(bad, prepared, error), "reject invalid vertex bone palette index");

    // Exercise config and routing without a network request or a WoW installation.
    auto temp = std::filesystem::temp_directory_path() / ("wow-retail-tests-" + std::to_string(std::rand()));
    std::filesystem::create_directories(temp);
    std::ofstream(temp / "aliases.csv") << "42;Creature/Test/Test.m2\n";
    std::ofstream(temp / "listfile.csv") << "43;Interface/Icons/Test.blp\n";
    std::ofstream(temp / "equipment.csv") << "# hand appearance\n123;916541;916543\n";
    std::ofstream(temp / "RetailCasc.ini") << "mode=online\npath=cache\nbuild=0123456789abcdef0123456789abcdef\naliases=aliases.csv\nlistfile=listfile.csv\nequipment=equipment.csv\n";
    Retail::CascSource source;
    Check(source.Configure((temp / "RetailCasc.ini").string().c_str(), error), error.c_str());
    Check(source.Handles("RETAIL\\43.blp"), "case/slash independent namespace");
    Check(source.Handles("creature\\test\\test.m2"), "explicit replacement alias");
    Check(source.Handles("retail/interface/icons/test.blp"), "listfile stays in Retail namespace");
    Check(!source.Handles("interface/icons/test.blp"), "no implicit stock overrides");
    Retail::HandItemAppearance appearance;
    Check(source.HandItem(123, appearance) && appearance.modelId == 916541 && appearance.textureId == 916543, "equipment uses Wrath display ID and Retail file IDs");
    Check(!source.HandItem(124, appearance), "unmapped equipment unchanged");
    Check(!source.SetHandItem(0, {1, 2}) && !source.SetHandItem(124, {1, 0}), "reject incomplete equipment override");
    Check(source.SetHandItem(123, {456, 789}) && source.HandItem(123, appearance) && appearance.modelId == 456, "session equipment override");
    Check(source.SetMainHandPreview({916541, 916543}) && source.HandItem(124, appearance, true) && appearance.modelId == 916541, "smoke test follows every equipped mainhand");
    Check(!source.HandItem(124, appearance, false), "mainhand smoke test does not replace offhand");
    source.ReleaseModel("retail/unknown.m2"); // A failed/non-Retail model has no retained owner.
    std::ofstream(temp / "bad.ini") << "mode=online\npath=cache\n";
    Retail::CascSource invalid;
    Check(!invalid.Configure((temp / "bad.ini").string().c_str(), error), "require pinned online build");
    std::ofstream(temp / "equipment.csv") << "123;916541;916543\n123;1;2\n";
    Retail::CascSource duplicateEquipment;
    Check(!duplicateEquipment.Configure((temp / "RetailCasc.ini").string().c_str(), error), "reject ambiguous equipment mappings");
    std::ofstream(temp / "equipment.csv") << "123;916541;0\n";
    Retail::CascSource invalidEquipment;
    Check(!invalidEquipment.Configure((temp / "RetailCasc.ini").string().c_str(), error), "reject missing equipment texture");
    std::ofstream(temp / "offline.ini") << "mode=files\npath=.\nbuild=0123456789abcdef0123456789abcdef\n";
    std::ofstream(temp / "42.bin", std::ios::binary).write(reinterpret_cast<const char*>(valid.data()), valid.size());
    Retail::CascSource offline;
    Check(offline.Configure((temp / "offline.ini").string().c_str(), error), "configure offline originals without network");
    Check(offline.Inspect("retail/42.m2", prepared, error) && prepared == valid, "offline source preserves original bytes");
    Check(!offline.Inspect("retail/43.m2", prepared, error), "offline source fails on missing dependency");
    // Remove only the individual files created here; no recursive cleanup of computed paths.
    for (const char* file : {"aliases.csv", "listfile.csv", "equipment.csv", "RetailCasc.ini", "bad.ini", "offline.ini", "42.bin"}) std::filesystem::remove(temp / file);
    std::filesystem::remove(temp);
    if (argc == 3 && std::string(argv[1]) == "--shader-bls") {
        std::ifstream input(argv[2], std::ios::binary);
        std::vector<uint8_t> bls((std::istreambuf_iterator<char>(input)), {});
        Check(bls.size() >= 12 && Get(bls, 0) == 0x47585348 && Get(bls, 8) == 16, "stock shader fixture header");
        size_t p = 12;
        for (unsigned i = 0; i < 16; ++i) {
            Check(p + 16 <= bls.size(), "shader record bounds");
            const size_t count = Get(bls, p + 12); p += 16;
            Check(count <= bls.size() - p, "shader bytecode bounds");
            const std::vector<uint8_t> original(bls.begin() + p, bls.begin() + p + count);
            Check(Retail::AddGlowLayer(original, prepared, error), error.c_str());
            Check(prepared.size() > original.size() && Get(prepared, 0) == 0xffff0300, "third texture layer added to every permutation");
            auto corrupt = original; corrupt.pop_back();
            Check(!Retail::AddGlowLayer(corrupt, prepared, error), "reject malformed shader bytecode");
            p = (p + count + 3) & ~size_t(3);
        }
        Check(p == bls.size(), "complete shader fixture consumed");
    } else Check(argc == 1, "unknown test arguments");
    std::puts("Retail asset validation and routing passed.");
    return 0;
}
