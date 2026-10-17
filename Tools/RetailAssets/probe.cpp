#include "Engine/Source/Retail/CascSource.h"
#include "Engine/Source/Retail/RetailModel.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 3 && argc != 4 && argc != 5) {
        std::fprintf(stderr, "Usage: RetailAssetProbe <RetailCasc.ini> <retail/ID.m2|alias> [--inspect|--extract new-file]\n");
        return 2;
    }
    const bool extract = argc == 5 && std::string(argv[3]) == "--extract";
    const bool inspect = extract || (argc == 4 && std::string(argv[3]) == "--inspect");
    if (argc > 3 && !inspect) return 2;
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
            if (!Retail::PrepareModel(bytes, prepared, files, error))
                std::printf("Runtime support: %s\n", error.c_str());
            else
                std::printf("Runtime model layout supported; use without --inspect to validate skins too.\n");
        }
    }
    return 0;
}
