#include "Engine/Source/Retail/OfflineStorage.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
namespace gate = Retail::OfflineStorage;
namespace {
void Check(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void Put(const fs::path& path) { fs::create_directories(path.parent_path()); std::ofstream(path) << "synthetic metadata selection fixture\n"; }
void Refused(const std::string& path, const char* diagnostic = nullptr) {
    unsigned calls = 0; std::string error;
    const bool result = gate::OpenWithPolicy(true, path, [&](const std::string&) { ++calls; return true; }, error);
    Check(!result && calls == 0, "offline refusal must happen before the CASC/network backend");
    Check(!error.empty() && (!diagnostic || error.find(diagnostic) != std::string::npos), "explicit offline setup diagnostic");
}
void Allowed(const fs::path& path, const fs::path& expected) {
    unsigned calls = 0; std::string error, opened;
    const bool result = gate::OpenWithPolicy(true, path.string(), [&](const std::string& selected) { ++calls; opened = selected; return true; }, error);
    Check(result && calls == 1 && fs::path(opened) == expected, "genuine local metadata reaches the exact backend once");
}
}
int main() {
    const auto root = fs::temp_directory_path() / ("esteria-offline-casc-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    for (const char* suffix : {"versions", "wow-123.VeRsIoNs", "prefixversions"}) {
        auto file = root / suffix; Put(file); Refused(file.string(), "cached-online"); fs::remove(file);
    }
    const auto cache = root / "cache"; Put(cache / "versions");
    Refused(cache.string(), "cached-online");
    auto deep = cache;
    for (unsigned i = 0; i < 12; ++i) deep /= "d";
    Refused(deep.string(), "cached-online"); // More than the ineffective upstream six-level limit.
    auto backslashes = deep.string();
    for (auto& c : backslashes) if (c == '/') c = '\\';
#ifndef _WIN32
    // Keep POSIX's initial root slash; CASC_PATH normalizes the remaining separators.
    if (!backslashes.empty()) backslashes[0] = '/';
#endif
    Refused(backslashes, "cached-online");
    Put(root / ".build.info");
    Refused(deep.string(), "cached-online"); // Nearer versions wins over a farther real install.
    fs::remove(root / ".build.info");
    for (const char* marker : {".build.info", ".build.db", "data/.build.config"}) {
        const auto local = root / "local"; const auto metadata = local / marker; Put(metadata);
        Allowed((local / "nested"), metadata);
        Allowed(metadata, metadata);
        // Exact prefixed/case-varied local files precede the ancestor cache.
        if (std::string(marker) != "data/.build.config") {
            auto alias = cache / (std::string("alias") + marker); Put(alias); Allowed(alias, alias); fs::remove(alias);
        }
        fs::remove_all(local);
    }
    const auto local = cache / "child"; const auto metadata = local / ".build.info"; Put(metadata);
    Allowed(local, metadata); // Local metadata takes precedence when actually openable.
    fs::permissions(metadata, fs::perms::owner_read, fs::perm_options::replace);
    if (!gate::MetadataFile(metadata)) Refused(local.string(), "cached-online");
    else std::puts("SKIP: host can override read-only metadata permissions; Windows/non-root CI exercises this refusal");
    fs::permissions(metadata, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace);
    fs::remove_all(local);
    for (const char* path : {"http://example.invalid/cache/versions", "f://example.invalid/cache/versions", "ribbit:cache/versions", "flat-http:cache/versions", "file*http://example.invalid/master", "//example.invalid/share", "bad\npath"}) Refused(path, "plain bounded local path");
    Refused((root / "missing").string(), "missing or not openable");
    Check(gate::PlainLocalPath("Z:/work/prepared"), "ordinary Wine/Windows drive paths remain local paths");
    unsigned desktopCalls = 0; std::string error;
    Check(gate::OpenWithPolicy(false, "desktop-online-cache", [&](const std::string& value) { ++desktopCalls; return value == "desktop-online-cache"; }, error) && desktopCalls == 1,
        "ordinary desktop storage behavior remains unchanged");
    fs::remove_all(root);
    std::puts("PASS: pre-backend offline CASC metadata selection, ancestor/cache refusal, local metadata and desktop behavior");
}
