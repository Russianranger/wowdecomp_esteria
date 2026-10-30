#ifndef ENGINE_SOURCE_RETAIL_OFFLINESTORAGE_H
#define ENGINE_SOURCE_RETAIL_OFFLINESTORAGE_H

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

namespace Retail::OfflineStorage {

inline bool Failure(std::string& error, const char* message) { error = message; return false; }
inline char Fold(char value) { return value >= 'A' && value <= 'Z' ? char(value + 'a' - 'A') : value; }
inline bool EndsWith(const std::string& value, const char* suffix) {
    const std::string end(suffix);
    return value.size() >= end.size() && std::equal(end.begin(), end.end(), value.end() - end.size(),
        [](char a, char b) { return Fold(a) == Fold(b); });
}

inline bool PlainLocalPath(const std::string& value) {
    if (value.empty() || value.size() >= 260 || value.find_first_of("*?\r\n") != std::string::npos || value.find("://") != std::string::npos) return false;
    if (value.size() >= 2 && (value[0] == '/' || value[0] == '\\') && (value[1] == '/' || value[1] == '\\')) return false;
    for (size_t i = 0; i < value.size(); ++i) {
        const auto c = static_cast<unsigned char>(value[i]);
        if (c < 32 || c == 127) return false;
        // CascLib's stream providers and master-stream chains must never reach
        // FileStream_OpenFile. Only an ordinary Windows drive colon is allowed.
        if (c == ':' && (i != 1 || !((Fold(value[0]) >= 'a') && (Fold(value[0]) <= 'z')))) return false;
    }
    return true;
}

inline bool MetadataFile(const std::filesystem::path& path) {
    std::error_code code;
    if (!std::filesystem::is_regular_file(path, code) || code) return false;
    // Pinned CascLib probes metadata with FileStream_OpenFile(path, 0), which
    // requests read/write access. Matching only readable files could select a
    // marker that CascLib then skips in favor of an ancestor online cache.
    std::fstream stream(path, std::ios::binary | std::ios::in | std::ios::out);
    return stream.is_open();
}

inline bool Resolve(const std::string& input, std::string& selected, std::string& error) {
    selected.clear();
    if (!PlainLocalPath(input)) return Failure(error, "Local offline CASC requires a plain bounded local path; stream providers and master streams are disabled");
    try {
#ifdef _WIN32
        constexpr const char* staticMarker = "data\\.build.config";
#else
        constexpr const char* staticMarker = "data/.build.config";
#endif
        const char* markers[] = {".build.info", ".build.db", staticMarker, "versions"};
        auto accept = [&](const std::filesystem::path& path, const char* marker) {
            if (std::string(marker) == "versions") return Failure(error,
                "Local offline CASC refuses cached-online versions metadata; prepare mode=files assets or a complete local CASC installation");
            auto local = path; local.make_preferred(); selected = local.string();
            return true;
        };
        // CascLib accepts arbitrary filename prefixes before these suffixes and
        // probes an explicit file before searching directories. Do not inspect
        // only a literal <directory>/versions file.
        for (const char* marker : markers)
            if (EndsWith(input, marker) && MetadataFile(std::filesystem::path(input)))
                return accept(std::filesystem::path(input), marker);
        // CASC_PATH normalizes both separator styles before walking parents.
        std::string normalized = input;
        const char separator = static_cast<char>(std::filesystem::path::preferred_separator);
        std::replace(normalized.begin(), normalized.end(), '\\', separator);
        std::replace(normalized.begin(), normalized.end(), '/', separator);
        auto directory = std::filesystem::path(normalized).lexically_normal();
        // Upstream's dwLevelCount is never incremented: its actual search walks
        // all ancestors, not just the apparent six-level limit.
        for (;;) {
            for (const char* marker : markers) {
                const auto candidate = directory / marker;
                if (candidate.string().size() < 260 && MetadataFile(candidate)) return accept(candidate, marker);
            }
            const auto parent = directory.parent_path();
            if (parent.empty() || parent == directory) break;
            directory = parent;
        }
    } catch (const std::filesystem::filesystem_error&) {
        return Failure(error, "Local offline CASC metadata could not be inspected; prepared local assets are required");
    }
    return Failure(error, "Local offline CASC metadata is missing or not openable; prepare mode=files assets or a complete local CASC installation");
}

// Shared production admission point: the backend is not invoked at all when a
// cached-online marker, stream provider or missing local install is refused.
// The client must retain immutable managed inputs while the storage is in use.
// CascSource also cancels download callbacks during open if metadata changes.
template <class Open>
inline bool OpenWithPolicy(bool offline, const std::string& path, Open open, std::string& error) {
    if (!offline) return open(path);
    std::string selected;
    return Resolve(path, selected, error) && open(selected);
}

}
#endif
