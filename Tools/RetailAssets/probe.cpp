#include "Engine/Source/Retail/CascSource.h"
#include "Engine/Source/Retail/RetailModel.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 3 && argc != 4 && argc != 5) {
        std::fprintf(stderr, "Usage: RetailAssetProbe <RetailCasc.ini> <retail/ID.m2|alias> [--inspect|--extract new-file|--helmet-donor legacy.m2|--animation ID[:variation]]\n");
        return 2;
    }
    const bool extract = argc == 5 && std::string(argv[3]) == "--extract";
    const bool helmet = argc == 5 && std::string(argv[3]) == "--helmet-donor";
    const bool animation = argc == 5 && std::string(argv[3]) == "--animation";
    const bool inspect = extract || (argc == 4 && std::string(argv[3]) == "--inspect");
    if (argc > 3 && !inspect && !helmet && !animation) return 2;
    Retail::CascSource source;
    std::vector<uint8_t> bytes;
    std::string error;
    if (!source.Configure(argv[1], error) || !(inspect ? source.Inspect(argv[2], bytes, error) : source.Read(argv[2], bytes, error))) {
        std::fprintf(stderr, "%s: %s\n", argv[2], error.c_str());
        return 1;
    }
    if (extract) {
        std::ifstream existing(argv[4], std::ios::binary);
        if (existing.good()) { std::fprintf(stderr, "Extraction destination already exists\n"); return 1; }
        std::ofstream output(argv[4], std::ios::binary);
        if (!output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size())) return 1;
    }
    if (helmet) {
        std::ifstream input(argv[4], std::ios::binary);
        const std::vector<uint8_t> legacy((std::istreambuf_iterator<char>(input)), {});
        Retail::HelmetAnchor donor, target;
        if (!Retail::ReadHelmetAnchor(legacy, donor, error) || !Retail::ReadHelmetAnchor(bytes, target, error)) {
            std::fprintf(stderr, "Helmet anchor: %s\n", error.c_str());
            return 1;
        }
        const auto shift = Retail::HelmetTranslation(donor, target);
        std::printf("Helmet donor head %.6f %.6f %.6f socket %.6f %.6f %.6f\n",
            donor.head[0], donor.head[1], donor.head[2], donor.socket[0], donor.socket[1], donor.socket[2]);
        std::printf("Retail head %.6f %.6f %.6f socket %.6f %.6f %.6f\n",
            target.head[0], target.head[1], target.head[2], target.socket[0], target.socket[1], target.socket[2]);
        std::printf("Helmet local correction %.6f %.6f %.6f\n", shift[0], shift[1], shift[2]);
    }
    if (animation) {
        for (const char* cursor = argv[4]; *cursor;) {
            char* end = nullptr;
            const unsigned long id = std::strtoul(cursor, &end, 10);
            if (end == cursor || id > 65535) return 2;
            unsigned long variation = 0;
            if (*end == ':') {
                cursor = end + 1;
                variation = std::strtoul(cursor, &end, 10);
                if (end == cursor || variation > 65535) return 2;
            }
            if (*end && *end != ',') return 2;
            cursor = *end ? end + 1 : end;
            std::string key = argv[2];
            key.resize(key.find_last_of('.'));
            char suffix[40];
            std::snprintf(suffix, sizeof(suffix), "%04lu-%02lu.anim", id, variation);
            key += suffix;
            std::vector<uint8_t> anim;
            if (!source.Read(key.c_str(), anim, error)) {
                std::fprintf(stderr, "%s: %s\n", key.c_str(), error.c_str());
                return 1;
            }
            Retail::AnimationLayout layout;
            if (!Retail::InspectAnimation(anim.data(), anim.size(), layout, error)) return 1;
            auto word = [](const std::vector<uint8_t>& data, size_t at) {
                uint32_t value; std::memcpy(&value, data.data() + at, 4); return value;
            };
            const size_t count = word(bytes, 0x1c), sequences = word(bytes, 0x20);
            size_t sequence = count;
            for (size_t i = 0; i < count; ++i) {
                if (word(bytes, sequences + i * 64) == (id | (variation << 16))) { sequence = i; break; }
            }
            if (sequence == count) return 1;
            const auto span = layout.bones.present ? layout.bones : layout.model;
            uint32_t movingBones = 0;
            for (size_t i = 0; i < word(bytes, 0x2c); ++i) {
                const size_t bone = word(bytes, 0x30) + i * 88;
                bool moving = false;
                for (size_t t = 0; t < 3; ++t) {
                    const size_t track = bone + 16 + t * 20;
                    if ((word(bytes, track) >> 16) != 0xffff) continue; // globals remain in the model
                    if (sequence >= word(bytes, track + 12)) continue;
                    const size_t pair = word(bytes, track + 16) + sequence * 8;
                    const size_t keys = word(bytes, pair), offset = span.offset + word(bytes, pair + 4);
                    size_t stride = t == 1 ? 8 : 12;
                    if ((word(bytes, track) & 0xffff) >= 2) stride *= 3;
                    for (size_t k = 1; k < keys; ++k)
                        moving = moving || std::memcmp(anim.data() + offset, anim.data() + offset + k * stride, stride) != 0;
                }
                if (moving) ++movingBones;
            }
            std::printf("Animation %lu:%lu validated: %zu bytes; model %zu, skeleton bones %zu, attachments %zu; %u bones have changing keys\n",
                id, variation, anim.size(), layout.model.size, layout.bones.size, layout.attachments.size, movingBones);
            if (!movingBones) { std::fprintf(stderr, "No skeletal motion in the requested animation\n"); return 1; }
        }
        return 0;
    }
    std::printf("%s: %zu decoded bytes (%s)\n", argv[2], bytes.size(), inspect ? "original CDN data" : "client-ready memory");
    if (bytes.size() >= 48 && std::memcmp(bytes.data(), "SKIN", 4) == 0) {
        uint32_t words[12];
        std::memcpy(words, bytes.data(), sizeof(words));
        std::printf("SKIN vertices %u; indices %u; bone indices %u; sections %u; batches %u; max bones %u\n",
            words[1], words[3], words[5], words[7], words[9], words[11]);
        if (words[7] && words[8] <= bytes.size() && 48 <= bytes.size() - words[8]) {
            uint16_t section[10];
            std::memcpy(section, bytes.data() + words[8], sizeof(section));
            std::printf("First section: %u vertices; %u indices; %u bones; combo %u; influences %u\n",
                section[3], section[5], section[6], section[7], section[8]);
        }
        if (words[9] && words[10] <= bytes.size() && 24 <= bytes.size() - words[10]) {
            uint16_t batch[12];
            std::memcpy(batch, bytes.data() + words[10], sizeof(batch));
            std::printf("First batch: shader 0x%04X; %u textures; combos %u/%u/%u/%u\n",
                batch[1], batch[7], batch[8], batch[9], batch[10], batch[11]);
        }
    }
    if (bytes.size() >= 4 && bytes[0] == 'M' && bytes[1] == 'D') {
        Retail::ModelFiles files;
        if (!Retail::InspectModel(bytes, files, error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        std::printf("M2 version %u; skeleton %u; %zu skins, %zu textures, %zu animations\n",
            files.version, files.skeleton, files.skins.size(), files.textures.size(), files.animations.size());
        if (bytes.size() >= 0x138) {
            const size_t body = bytes[3] == '1' ? 8 : 0;
            uint32_t count;
            for (size_t offset : {size_t(0x48), size_t(0x70), size_t(0x80), size_t(0x88), size_t(0x90), size_t(0x98)}) {
                std::memcpy(&count, bytes.data() + body + offset, 4);
                std::printf("  table 0x%zX: %u entries\n", offset, count);
            }
        }
        for (auto id : files.skins) std::printf("  skin %u\n", id);
        for (auto id : files.textures) std::printf("  texture %u\n", id);
        if (inspect) {
            std::vector<uint8_t> prepared;
            std::vector<uint8_t> skeleton;
            if (files.skeleton && !source.Inspect(Retail::FileName(files.skeleton, ".skel").c_str(), skeleton, error))
                std::printf("Skeleton fetch: %s\n", error.c_str());
            if (!Retail::PrepareModel(bytes, prepared, files, error, skeleton.empty() ? nullptr : &skeleton))
                std::printf("Runtime support: %s\n", error.c_str());
            else
                std::printf("Runtime model layout supported; use without --inspect to validate skins too.\n");
        }
    }
    return 0;
}
