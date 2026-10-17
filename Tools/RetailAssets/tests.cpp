#include "Engine/Source/Retail/CascSource.h"
#include "Engine/Source/Retail/RetailModel.h"
#include "Engine/Source/Retail/ClientTables.h"
#include "Engine/Source/Retail/RetailShader.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
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
    std::string error;
    Retail::ModelFiles files;
    std::vector<uint8_t> prepared;
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
