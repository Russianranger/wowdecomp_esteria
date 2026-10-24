#include "EmbeddedFiles.h"
#include <cctype>
#include <map>
#include <string>
#include <utility>

static std::map<std::string, std::pair<const uint8_t*, uint32_t>>& Files() {
    static std::map<std::string, std::pair<const uint8_t*, uint32_t>> files;
    return files;
}

static std::string Normalize(const char* name) {
    std::string key(name);

    for (char& c : key) {
        c = c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    return key;
}

void EmbeddedFileRegister(const char* name, const uint8_t* data, uint32_t size) {
    Files()[Normalize(name)] = { data, size };
}

bool EmbeddedFileFind(const char* name, const uint8_t** data, uint32_t* size) {
    if (Files().empty()) {
        return false;
    }

    auto it = Files().find(Normalize(name));

    if (it == Files().end()) {
        return false;
    }

    *data = it->second.first;
    *size = it->second.second;
    return true;
}
