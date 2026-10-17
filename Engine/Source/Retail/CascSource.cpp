#include "Engine/Source/Retail/CascSource.h"
#include "Engine/Source/Retail/RetailModel.h"
#include "Engine/Source/Retail/ClientTables.h"
#include <CascLib.h>
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

namespace Retail {
namespace {
std::string Normalize(std::string name) {
    for (auto& c : name) {
        if (c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    }
    return name;
}
std::string Trim(std::string text) {
    const auto start = text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    return text.substr(start, text.find_last_not_of(" \t\r\n") - start + 1);
}
bool Number(const std::string& s, uint32_t& result, int base = 10) {
    auto parsed = std::from_chars(s.data(), s.data() + s.size(), result, base);
    return !s.empty() && parsed.ec == std::errc() && parsed.ptr == s.data() + s.size() && result;
}
bool IsRetail(const std::string& name) { return name.compare(0, 7, "retail/") == 0; }
std::string Extension(const std::string& name) {
    const auto dot = name.find_last_of('.');
    return dot == std::string::npos ? "" : name.substr(dot);
}
bool SafeName(const std::string& name) {
    if (name.empty() || name.front() == '/' || name.find(':') != std::string::npos) return false;
    std::istringstream parts(name);
    std::string part;
    while (std::getline(parts, part, '/')) if (part == ".." || part.empty()) return false;
    return true;
}
bool TableName(const std::string& name) {
    for (const char* table : {"item.dbc", "itemdisplayinfo.dbc", "creaturemodeldata.dbc", "creaturedisplayinfo.dbc",
        "spell.dbc", "spellicon.dbc", "skilllineability.dbc"})
        if (name == std::string("dbfilesclient/") + table) return true;
    return false;
}
bool Failure(std::string& error, const char* message) { error = message; return false; }
bool CascFailure(std::string& error, const char* operation) {
    error = std::string(operation) + " (CascLib error " + std::to_string(GetCascError()) + ")";
    return false;
}
struct File {
    HANDLE handle = nullptr;
    ~File() { if (handle) CascCloseFile(handle); }
};
struct Model {
    std::vector<uint8_t> bytes;
    ModelFiles files;
};
struct Reference {
    uint32_t id = 0;
    std::shared_ptr<Model> owner;
    uint16_t animation = 0;
    uint16_t variation = 0;
};
}

struct CascSource::Impl {
    std::mutex mutex;
    HANDLE storage = nullptr;
    std::string path, product = "wow", region = "us", build, keys, tables;
    uint32_t locale = CASC_LOCALE_ENUS;
    bool online = true, configured = false, files = false;
    std::string configurationError = "RetailCasc.ini has not been configured";
    std::map<std::string, uint32_t> names;
    std::map<std::string, Reference> dependencies;
    std::map<std::string, std::pair<std::shared_ptr<Model>, size_t>> models;
    std::map<std::string, uint32_t> cachedSizes;
    std::map<uint32_t, HandItemAppearance> handItems;
    HandItemAppearance mainHandPreview;
    ~Impl() { if (storage) CascCloseStorage(storage); }

    bool Open(std::string& error) {
        if (!configured) { error = configurationError; return false; }
        if (storage) return true;
        CASC_OPEN_STORAGE_ARGS args = {};
        args.Size = sizeof(args);
        args.szLocalPath = path.c_str();
        args.szCodeName = product.c_str();
        args.szRegion = region.c_str();
        args.szBuildKey = build.empty() ? nullptr : build.c_str();
        args.dwLocaleMask = locale;
        if (!CascOpenStorageEx(nullptr, &args, online, &storage)) return CascFailure(error, "Cannot open CASC storage");
        if (!keys.empty() && !CascImportKeysFromFile(storage, keys.c_str())) {
            CascFailure(error, "Cannot import encryption keys");
            CascCloseStorage(storage);
            storage = nullptr;
            return false;
        }
        return true;
    }

    bool Resolve(const std::string& name, Reference& ref, std::string& internal, std::string& error) {
        if (!SafeName(name)) return Failure(error, "Invalid Retail virtual path");
        const auto dep = dependencies.find(name);
        if (dep != dependencies.end()) { ref = dep->second; return true; }
        const auto alias = names.find(name);
        if (alias != names.end()) { ref.id = alias->second; return true; }
        if (!IsRetail(name)) return Failure(error, "No Retail alias for this path");
        internal = name.substr(7);
        const auto dot = internal.find('.');
        const auto stem = internal.substr(0, dot);
        if (Number(stem, ref.id)) { internal.clear(); return true; }
        // Numeric-looking names must not silently turn an overflowing ID into a path.
        if (stem.find_first_not_of("0123456789") == std::string::npos)
            return Failure(error, "Invalid Retail FileDataID");
        return true;
    }

    bool OpenFile(const Reference& ref, const std::string& internal, File& file, std::string& error) {
        if (!Open(error)) return false;
        const void* name = ref.id ? CASC_FILE_DATA_ID(ref.id) : internal.c_str();
        DWORD flags = (ref.id ? CASC_OPEN_BY_FILEID : CASC_OPEN_BY_NAME) | CASC_STRICT_DATA_CHECK;
        if (!CascOpenFile(storage, name, locale, flags, &file.handle)) return CascFailure(error, "Cannot open Retail file");
        return true;
    }

    bool Fetch(const Reference& ref, const std::string& internal, std::vector<uint8_t>& bytes, std::string& error) {
        if (files) {
            if (!configured || !ref.id) return Failure(error, "Offline Retail packs require configured FileDataID references");
            std::ifstream input(std::filesystem::path(path) / (std::to_string(ref.id) + ".bin"), std::ios::binary | std::ios::ate);
            if (!input) return Failure(error, "Missing original asset in offline Retail pack");
            const auto size = input.tellg();
            if (size < 0 || size > 128 * 1024 * 1024) return Failure(error, "Invalid offline Retail asset size");
            bytes.resize(static_cast<size_t>(size));
            input.seekg(0);
            if (!input.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) return Failure(error, "Cannot read offline Retail asset");
            return true;
        }
        File file;
        if (!OpenFile(ref, internal, file, error)) return false;
        ULONGLONG size = 0;
        if (!CascGetFileSize64(file.handle, &size)) return CascFailure(error, "Cannot get Retail file size");
        if (size > 128 * 1024 * 1024) return Failure(error, "Retail file exceeds 128 MiB limit");
        bytes.resize(static_cast<size_t>(size));
        DWORD read = 0;
        if (size && (!CascReadFile(file.handle, bytes.data(), static_cast<DWORD>(size), &read) || read != size)) {
            bytes.clear();
            return CascFailure(error, "Cannot read Retail file (missing data or encryption key)");
        }
        return true;
    }
};

CascSource::CascSource() : m_impl(new Impl) {}
CascSource::~CascSource() = default;

bool CascSource::Configure(const char* configPath, std::string& error) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    auto& s = *m_impl;
    error.clear();
    if (s.storage || s.configured) return Failure(error, "CASC source is already configured; restart to change builds");
    try {
    // Failed configuration is retained for the client's diagnostic log.
    struct Remember { Impl& state; std::string& error; ~Remember() { state.configurationError = error; } } remember{s, error};
    std::ifstream input(configPath);
    if (!input) return Failure(error, "Cannot read Retail CASC config; set WOW_RETAIL_CONFIG or create RetailCasc.ini");
    std::map<std::string, std::string> values;
    std::string line;
    while (std::getline(input, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        auto equal = line.find('=');
        if (equal == std::string::npos) return Failure(error, "Expected key=value in Retail CASC config");
        auto key = Normalize(Trim(line.substr(0, equal)));
        if (!values.emplace(key, Trim(line.substr(equal + 1))).second) return Failure(error, "Duplicate Retail CASC config key");
    }
    for (const auto& value : values) {
        if (value.first != "mode" && value.first != "path" && value.first != "product" && value.first != "region" &&
            value.first != "build" && value.first != "locale" && value.first != "aliases" && value.first != "listfile" && value.first != "keys" && value.first != "equipment" && value.first != "tables")
            return Failure(error, "Unknown Retail CASC config key");
    }
    if (values["mode"] != "online" && values["mode"] != "local" && values["mode"] != "files") return Failure(error, "mode must be online, local or files");
    s.online = values["mode"] == "online";
    s.files = values["mode"] == "files";
    if (!values["product"].empty()) s.product = values["product"];
    if (!values["region"].empty()) s.region = values["region"];
    if (s.product != "wow" && s.product != "wowt" && s.product != "wow_beta") return Failure(error, "Unsupported Retail product");
    if (s.region.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos || s.region.empty())
        return Failure(error, "Invalid Retail region");
    s.build = Normalize(values["build"]);
    if ((s.online || s.files || !s.build.empty()) && (s.build.size() != 32 || s.build.find_first_not_of("0123456789abcdef") != std::string::npos))
        return Failure(error, "Online mode requires an explicit 32-digit build config key");
    if (!values["locale"].empty() && !Number(values["locale"], s.locale, 16)) return Failure(error, "locale must be a nonzero hexadecimal locale mask");
    const auto base = std::filesystem::absolute(configPath).parent_path();
    auto absolute = [&](const std::string& text) { return (base / std::filesystem::path(text)).lexically_normal(); };
    if (values["path"].empty()) return Failure(error, "CASC storage/cache path is required");
    auto path = absolute(values["path"]);
    if (s.online) {
        path /= s.product + "-" + s.build;
        // CascLib searches ancestor build files; keep online caches outside game installs.
        for (auto parent = path; !parent.empty();) {
            for (const char* file : {".build.info", ".build.db", "data/.build.config"})
                if (std::filesystem::exists(parent / file)) return Failure(error, "Online cache must be outside installed CASC storages");
            if (parent != path && std::filesystem::exists(parent / "versions"))
                return Failure(error, "Online cache must not be nested beneath another versions manifest");
            auto next = parent.parent_path();
            if (next == parent) break;
            parent = next;
        }
    }
    s.path = path.string();
    s.tables = values["tables"].empty() ? "" : absolute(values["tables"]).string();
    s.keys = values["keys"].empty() ? "" : absolute(values["keys"]).string();
    std::map<std::string, uint32_t> names;
    for (const char* kind : {"listfile", "aliases"}) {
        if (values[kind].empty()) continue;
        std::ifstream csv(absolute(values[kind]));
        if (!csv) return Failure(error, "Cannot read Retail listfile/aliases");
        while (std::getline(csv, line)) {
            line = Trim(line);
            if (line.empty() || line[0] == '#') continue;
            auto separator = line.find(';');
            uint32_t id = 0;
            if (separator == std::string::npos || !Number(Trim(line.substr(0, separator)), id))
                return Failure(error, "Retail mapping must use FileDataID;path rows");
            auto name = Normalize(Trim(line.substr(separator + 1)));
            if (!SafeName(name)) return Failure(error, "Invalid Retail mapping path");
            if (std::string(kind) == "listfile") name = "retail/" + name;
            const auto result = names.emplace(name, id);
            if (!result.second && result.first->second != id) return Failure(error, "Conflicting Retail mappings");
        }
    }
    std::map<uint32_t, HandItemAppearance> handItems;
    if (!values["equipment"].empty()) {
        std::ifstream csv(absolute(values["equipment"]));
        if (!csv) return Failure(error, "Cannot read Retail equipment mapping");
        while (std::getline(csv, line)) {
            line = Trim(line);
            if (line.empty() || line[0] == '#') continue;
            const auto first = line.find(';'), second = line.find(';', first == std::string::npos ? first : first + 1);
            uint32_t display = 0;
            HandItemAppearance appearance;
            if (first == std::string::npos || second == std::string::npos ||
                !Number(Trim(line.substr(0, first)), display) ||
                !Number(Trim(line.substr(first + 1, second - first - 1)), appearance.modelId) ||
                !Number(Trim(line.substr(second + 1)), appearance.textureId))
                return Failure(error, "Retail equipment rows must be WrathDisplayID;modelFileDataID;textureFileDataID (positive IDs)");
            if (!handItems.emplace(display, appearance).second) return Failure(error, "Duplicate Retail equipment display ID");
        }
    }
    s.names = std::move(names);
    s.handItems = std::move(handItems);
    s.configured = true;
    return true;
    } catch (const std::filesystem::filesystem_error& exception) {
        error = std::string("Invalid or inaccessible Retail CASC path: ") + exception.what();
        s.configurationError = error;
        return false;
    }
}

bool CascSource::HandItem(uint32_t displayId, HandItemAppearance& appearance, bool mainHand) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (mainHand && m_impl->mainHandPreview.modelId) {
        appearance = m_impl->mainHandPreview;
        return true;
    }
    const auto found = m_impl->handItems.find(displayId);
    if (found == m_impl->handItems.end()) return false;
    appearance = found->second;
    return true;
}

bool CascSource::SetHandItem(uint32_t displayId, HandItemAppearance appearance) {
    if (!displayId || !appearance.modelId || !appearance.textureId) return false;
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (!m_impl->configured) return false;
    m_impl->handItems[displayId] = appearance;
    return true;
}

bool CascSource::SetMainHandPreview(HandItemAppearance appearance) {
    if (!appearance.modelId || !appearance.textureId) return false;
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (!m_impl->configured) return false;
    m_impl->mainHandPreview = appearance;
    return true;
}

bool CascSource::Handles(const char* name) {
    if (!name) return false;
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    const auto key = Normalize(name);
    return IsRetail(key) || m_impl->names.count(key) || m_impl->dependencies.count(key);
}

bool CascSource::HasClientTable(const char* name) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    const auto key = Normalize(name ? name : "");
    return m_impl->configured && !m_impl->tables.empty() &&
        TableName(key) && std::filesystem::is_regular_file(std::filesystem::path(m_impl->tables) / key.substr(14));
}

bool CascSource::MergeTable(const char* name, const std::vector<uint8_t>& base,
    std::vector<uint8_t>& merged, std::string& error) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    const auto key = Normalize(name ? name : "");
    if (!TableName(key))
        return Failure(error, "Unsupported supplemental table name");
    const auto table = key.substr(14);
    std::ifstream input(std::filesystem::path(m_impl->tables) / table, std::ios::binary | std::ios::ate);
    if (!input) return Failure(error, "Missing configured supplemental client table");
    const auto size = input.tellg();
    if (size < 0 || size > 128 * 1024 * 1024) return Failure(error, "Supplemental table exceeds size limit");
    std::vector<uint8_t> additions(static_cast<size_t>(size));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(additions.data()), additions.size())) return Failure(error, "Cannot read supplemental table");
    return MergeClientTable(table, base, additions, merged, error);
}

bool CascSource::Exists(const char* name, std::string& error) {
    error.clear();
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    Reference ref;
    std::string internal;
    File file;
    if (!m_impl->Resolve(Normalize(name ? name : ""), ref, internal, error)) return false;
    if (m_impl->files) return ref.id && std::filesystem::is_regular_file(std::filesystem::path(m_impl->path) / (std::to_string(ref.id) + ".bin"));
    return m_impl->OpenFile(ref, internal, file, error);
}

bool CascSource::Inspect(const char* name, std::vector<uint8_t>& bytes, std::string& error) {
    error.clear();
    bytes.clear();
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    Reference ref;
    std::string internal;
    return m_impl->Resolve(Normalize(name ? name : ""), ref, internal, error) && m_impl->Fetch(ref, internal, bytes, error);
}

bool CascSource::Read(const char* name, std::vector<uint8_t>& bytes, std::string& error) {
    error.clear();
    bytes.clear();
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    const std::string key = Normalize(name ? name : "");
    Reference ref;
    std::string internal;
    auto& s = *m_impl;
    if (!s.Resolve(key, ref, internal, error) || !s.Fetch(ref, internal, bytes, error)) return false;
    const auto ext = Extension(key);
    bool success = true;
    if (ext == ".m2" || ext == ".mdx") {
        auto model = std::make_shared<Model>();
        success = PrepareModel(bytes, model->bytes, model->files, error);
        std::map<std::string, Reference> pending;
        const auto base = key.substr(0, key.find_last_of('.'));
        // Check every advertised profile before exposing the model to the renderer.
        for (size_t i = 0; success && i < model->files.skins.size(); ++i) {
            if (i >= 4 || i >= (model->bytes.size() > 0x44 ? model->bytes[0x44] : 0)) break;
            Reference skin{model->files.skins[i], model};
            std::vector<uint8_t> raw;
            success = s.Fetch(skin, "", raw, error) && PrepareSkin(raw, model->bytes, error);
            char suffix[16];
            std::snprintf(suffix, sizeof(suffix), "%02u.skin", static_cast<unsigned>(i));
            pending.emplace(base + suffix, std::move(skin));
        }
        for (const auto& anim : model->files.animations) {
            if (!anim.fileId) continue;
            char suffix[32];
            std::snprintf(suffix, sizeof(suffix), "%04u-%02u.anim", anim.id, anim.variation);
            auto added = pending.emplace(base + suffix, Reference{anim.fileId, model, anim.id, anim.variation});
            if (!added.second && added.first->second.id != anim.fileId) success = Failure(error, "Conflicting AFID entries");
        }
        if (success) {
            // Same name always denotes the same file within a source's pinned build.
            auto& owner = s.models[key];
            if (!owner.first) owner.first = model;
            ++owner.second;
            for (auto& dep : pending) {
                dep.second.owner = owner.first;
                s.dependencies[dep.first] = dep.second;
            }
            bytes = model->bytes;
        }
    } else if (ext == ".skin") {
        success = ref.owner ? PrepareSkin(bytes, ref.owner->bytes, error) : Failure(error, "Open the Retail model before its skin");
    } else if (ext == ".anim") {
        success = ref.owner ? (StripAnimationChunk(bytes, error) && ValidateAnimation(bytes, ref.owner->bytes, ref.animation, ref.variation, error)) :
            Failure(error, "Open the Retail model before its animation");
    } else if (ext == ".blp") {
        // BLP2 format version 1 is the existing client's native texture format.
        success = bytes.size() >= 148 && std::memcmp(bytes.data(), "BLP2\1\0\0\0", 8) == 0;
        if (!success) error = "Retail texture is not a supported BLP2 file";
    } else {
        success = Failure(error, "Retail runtime supports M2, SKIN, ANIM and BLP references only");
    }
    if (!success) bytes.clear();
    else s.cachedSizes[key] = static_cast<uint32_t>(bytes.size());
    return success;
}

bool CascSource::CachedSize(const char* name, uint32_t& size) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    const auto found = m_impl->cachedSizes.find(Normalize(name ? name : ""));
    if (found == m_impl->cachedSizes.end()) return false;
    size = found->second;
    return true;
}

void CascSource::ReleaseModel(const char* name) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    const auto model = m_impl->models.find(Normalize(name ? name : ""));
    if (model == m_impl->models.end() || --model->second.second) return;
    for (auto it = m_impl->dependencies.begin(); it != m_impl->dependencies.end();) {
        if (it->second.owner == model->second.first) it = m_impl->dependencies.erase(it);
        else ++it;
    }
    m_impl->models.erase(model);
}

CascSource& Source() {
    static CascSource source;
    static std::once_flag initialized;
    std::call_once(initialized, [&] {
        const char* path = std::getenv("WOW_RETAIL_CONFIG");
        std::string error;
        if (!source.Configure(path && *path ? path : "RetailCasc.ini", error))
            std::fprintf(stderr, "Retail CASC: %s\n", error.c_str());
    });
    return source;
}
}
