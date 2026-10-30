#include "Engine/Source/Platform/LocalAccountHandoff.h"
#if defined(_WIN32)
#include "Engine/Source/Platform/PrivateLocalAccountFile.h"
#endif
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>
#if defined(_WIN32)
#include <string>
#endif

namespace {
using Esteria::LocalAccount::Credentials;
using Esteria::LocalAccount::InputBytes;
using Esteria::LocalAccount::InputKind;
using Esteria::LocalAccount::Parse;
using Esteria::LocalLogin::Attempt;
using Esteria::LocalLogin::Readiness;
using Esteria::LocalLogin::Result;
using Bytes = std::array<unsigned char, InputBytes>;

void Require(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}
bool Zero(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) if (bytes[i]) return false;
    return true;
}
bool Cleared(const Credentials& value) {
    return Zero(value.nonce, sizeof(value.nonce)) && Zero(value.username, sizeof(value.username)) &&
        Zero(value.password, sizeof(value.password));
}
void Seed(Credentials& value) {
    std::memset(value.nonce, 0x5a, sizeof(value.nonce));
    std::memset(value.username, 'X', sizeof(value.username));
    std::memset(value.password, 'X', sizeof(value.password));
}
Bytes Fixture(InputKind kind = InputKind::Login) {
    Bytes raw{};
    std::memcpy(raw.data(), kind == InputKind::Login ? "ESTLOGN1" : "ESTAUTH1", 8);
    for (unsigned i = 0; i < 16; ++i) raw[8 + i] = static_cast<unsigned char>(i * 17);
    std::memcpy(raw.data() + 24, "EST0123456789ABCD", 17);
    std::memcpy(raw.data() + 41, "ABCD2345EFGH6789", 16);
    return raw;
}
bool Equal(const Credentials& value, const Bytes& raw) {
    return !std::memcmp(value.nonce, raw.data() + 8, 16) &&
        !std::memcmp(value.username, raw.data() + 24, 17) && value.username[17] == 0 &&
        !std::memcmp(value.password, raw.data() + 41, 16) && value.password[16] == 0;
}
bool Allowed(std::size_t offset, unsigned char value, const Bytes& original) {
    if (offset < 8 || (offset >= 24 && offset < 27)) return value == original[offset];
    if (offset < 24) return true; // The opaque nonce has no text interpretation.
    return value && std::strchr(offset < 41 ? "0123456789ABCDEF" : "ABCDEFGHJKLMNPQRSTUVWXYZ23456789", value);
}
void ParserTests() {
    static_assert(!std::is_copy_constructible_v<Credentials> && !std::is_copy_assignable_v<Credentials>);
    Credentials output;
    for (const auto kind : {InputKind::Login, InputKind::Diagnostic}) {
        const Bytes original = Fixture(kind);
        Require(Parse(original.data(), original.size(), kind, output) && Equal(output, original), "valid bounded input");
        for (std::size_t size = 0; size <= InputBytes + 1; ++size) {
            if (size == InputBytes) continue;
            Seed(output);
            Require(!Parse(original.data(), size, kind, output) && Cleared(output), "wrong length clears output");
        }
        Seed(output);
        Require(!Parse(nullptr, InputBytes, kind, output) && Cleared(output), "null input clears output");
        Seed(output);
        Require(!Parse(original.data(), (std::numeric_limits<std::size_t>::max)(), kind, output) && Cleared(output),
            "unbounded size rejected before read");
        const auto other = kind == InputKind::Login ? InputKind::Diagnostic : InputKind::Login;
        Seed(output);
        Require(!Parse(original.data(), original.size(), other, output) && Cleared(output), "diagnostic and login formats separated");
        for (std::size_t offset = 0; offset < InputBytes; ++offset) {
            for (unsigned value = 0; value <= 255; ++value) {
                Bytes mutated = original;
                mutated[offset] = static_cast<unsigned char>(value);
                const Bytes unchanged = mutated;
                Seed(output);
                const bool accepted = Parse(mutated.data(), mutated.size(), kind, output);
                Require(accepted == Allowed(offset, static_cast<unsigned char>(value), original), "exhaustive byte validation");
                Require(accepted ? Equal(output, mutated) : Cleared(output), "complete output or cleared failure");
                Require(mutated == unchanged, "parser preserves source bytes");
            }
        }
    }
    Seed(output);
    output.Clear();
    Require(Cleared(output), "explicit credential clear");
    alignas(Credentials) unsigned char storage[sizeof(Credentials)]{};
    auto* owned = new (storage) Credentials();
    Seed(*owned);
    owned->~Credentials();
    Require(Zero(storage, sizeof(storage)), "credential destructor clears owned buffers");
}
Readiness Ready() {
    Readiness value;
    value.initialized = value.framesLoaded = value.loginScreen = value.idle = value.scanReady = true;
    value.tosAccepted = value.eulaAccepted = value.clientServicesReady = true;
    return value;
}
void AttemptTests() {
    using namespace Esteria::LocalLogin;
    ConfigureAtStartup(false, false);
    Require(!Requested() && StartupValid(), "desktop startup default");
    ConfigureAtStartup(true, false);
    Require(Requested() && !StartupValid(), "local login requires offline startup");
    ConfigureAtStartup(true, true);
    Require(Requested() && StartupValid(), "explicit offline local login startup");
    ConfigureAtStartup(false, false);
    const auto raw = Fixture();
    unsigned reads = 0, submissions = 0;
    const auto read = [&](Credentials& value) { ++reads;return Parse(raw.data(), raw.size(), InputKind::Login, value); };
    const auto submit = [&](const char* username, char* password) {
        Require(!std::strcmp(username, "EST0123456789ABCD") && !std::strcmp(password, "ABCD2345EFGH6789"),
            "submission receives validated credentials");
        ++submissions;
        // The real Glue entry point clears the caller's password after copying it.
        Esteria::LocalAccount::SecureClear(password, 16);
    };
    Readiness ready = Ready();
    Require(ready.Ready(), "all readiness gates satisfied");
    Attempt dormant;
    Require(dormant.Run(false, false, ready, read, submit) == Result::Dormant && !reads && !submissions,
        "default path performs no credential IO");
    Attempt rejected;
    Require(rejected.Run(true, false, ready, read, submit) == Result::Rejected && !reads && !submissions,
        "offline gate precedes credential IO");
    bool Readiness::* const gates[] = {&Readiness::initialized, &Readiness::framesLoaded, &Readiness::suspended,
        &Readiness::reloading, &Readiness::loginScreen, &Readiness::idle, &Readiness::scanReady,
        &Readiness::tosAccepted, &Readiness::eulaAccepted, &Readiness::clientServicesReady};
    for (auto gate : gates) {
        Readiness blocked = ready;
        blocked.*gate = !(blocked.*gate);
        Require(!blocked.Ready(), "each readiness gate independently blocks login");
        Attempt pending;
        const unsigned before = reads;
        for (unsigned repeat = 0; repeat < 3; ++repeat)
            Require(pending.Run(true, true, blocked, read, submit) == Result::Waiting && reads == before,
                "unready idle retains handoff without reading");
        Require(pending.Run(true, true, ready, read, submit) == Result::Submitted && reads == before + 1,
            "ready transition submits once");
        Require(pending.Run(true, true, blocked, read, submit) == Result::AlreadyAttempted &&
            pending.Run(true, true, ready, read, submit) == Result::AlreadyAttempted && reads == before + 1,
            "reload and repeated idle cannot retry successful attempt");
    }
    Require(reads == 10 && submissions == 10, "one submission per independent process state");
    Attempt failed;
    unsigned failures = 0;
    const auto failRead = [&](Credentials& value) { ++failures;Seed(value);return false; };
    Require(failed.Run(true, true, ready, failRead, submit) == Result::ReadFailed && failures == 1 && submissions == 10,
        "failed consumer never submits");
    for (unsigned repeat = 0; repeat < 3; ++repeat)
        Require(failed.Run(true, true, ready, read, submit) == Result::AlreadyAttempted && reads == 10 && submissions == 10,
            "failed read cannot retry or substitute another handoff");
}

#if defined(_WIN32)
struct TemporaryDirectory {
    std::wstring path;
    TemporaryDirectory() {
        wchar_t root[MAX_PATH]{}, reserved[MAX_PATH]{};
        const DWORD size = GetTempPathW(MAX_PATH, root);
        Require(size && size < MAX_PATH && GetTempFileNameW(root, L"elh", 0, reserved), "private consumer test location");
        Require(DeleteFileW(reserved) && CreateDirectoryW(reserved, nullptr), "private consumer test directory");
        path = reserved;
    }
    std::wstring File(const wchar_t* name) const { return path + L"\\" + name; }
    ~TemporaryDirectory() {
        for (const auto* name : {L"alias.bin", L"input.bin", L"source.bin"}) {
            const auto file = File(name);
            SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_NORMAL);
            DeleteFileW(file.c_str());
        }
        RemoveDirectoryW(path.c_str());
    }
};
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
void Write(const std::wstring& path, const void* bytes, DWORD size) {
    Handle file;
    file.value = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    Require(file.value != INVALID_HANDLE_VALUE, "create private test input");
    DWORD written = 0;
    Require(WriteFile(file.value, bytes, size, &written, nullptr) && written == size && FlushFileBuffers(file.value),
        "complete private test input");
}
bool Exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }
void Preserved(const std::wstring& path, const void* expected, DWORD size) {
    Require(Exists(path), "rejected input preserved");
    Handle file;
    file.value = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    Require(file.value != INVALID_HANDLE_VALUE, "reopen rejected input");
    LARGE_INTEGER actual{};
    Require(GetFileSizeEx(file.value, &actual) && actual.QuadPart == size, "rejected input size preserved");
    unsigned char bytes[InputBytes + 1]{};
    DWORD read = 0;
    Require(size <= sizeof(bytes) && ReadFile(file.value, bytes, size, &read, nullptr) && read == size &&
        !std::memcmp(bytes, expected, size), "rejected input bytes preserved");
}
void ConsumerTests() {
    using Esteria::LocalAccount::ReadAndConsume;
    TemporaryDirectory directory;
    const auto input = directory.File(L"input.bin"), alias = directory.File(L"alias.bin"), source = directory.File(L"source.bin");
    const Bytes valid = Fixture();
    Credentials output;
    Seed(output);
    Require(!ReadAndConsume(input.c_str(), output) && Cleared(output), "absent private handoff rejected");
    Write(input, valid.data(), static_cast<DWORD>(valid.size()));
    Require(ReadAndConsume(input.c_str(), output) && Equal(output, valid) && !Exists(input), "valid handoff read and deleted");
    Seed(output);
    Require(!ReadAndConsume(input.c_str(), output) && Cleared(output), "consumed handoff cannot replay");
    for (const std::size_t offset : {std::size_t(0), std::size_t(24), std::size_t(27), std::size_t(41)}) {
        Bytes malformed = valid;
        malformed[offset] = 0;
        Write(input, malformed.data(), static_cast<DWORD>(malformed.size()));
        Seed(output);
        Require(!ReadAndConsume(input.c_str(), output) && Cleared(output), "malformed private handoff rejected");
        Preserved(input, malformed.data(), static_cast<DWORD>(malformed.size()));
        Require(DeleteFileW(input.c_str()) != 0, "remove rejected test input");
    }
    const Bytes diagnostic = Fixture(InputKind::Diagnostic);
    Write(input, diagnostic.data(), static_cast<DWORD>(diagnostic.size()));
    Require(!ReadAndConsume(input.c_str(), output) && Cleared(output), "diagnostic handoff cannot enter normal login");
    Preserved(input, diagnostic.data(), static_cast<DWORD>(diagnostic.size()));
    Require(DeleteFileW(input.c_str()) != 0, "remove diagnostic test input");
    unsigned char oversized[InputBytes + 1]{};
    std::memcpy(oversized, valid.data(), valid.size());
    for (const DWORD size : {DWORD(InputBytes - 1), DWORD(InputBytes + 1)}) {
        Write(input, oversized, size);
        Seed(output);
        Require(!ReadAndConsume(input.c_str(), output) && Cleared(output), "wrong sized private handoff rejected");
        Preserved(input, oversized, size);
        Require(DeleteFileW(input.c_str()) != 0, "remove sized test input");
    }
    Write(input, valid.data(), static_cast<DWORD>(valid.size()));
    {
        Handle shared;
        shared.value = CreateFileW(input.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, 0, nullptr);
        Require(shared.value != INVALID_HANDLE_VALUE, "open competing consumer");
        Seed(output);
        Require(!ReadAndConsume(input.c_str(), output) && Cleared(output) && Exists(input), "competing open blocks consumption");
    }
    Preserved(input, valid.data(), static_cast<DWORD>(valid.size()));
    Require(CreateHardLinkW(alias.c_str(), input.c_str(), nullptr) != 0, "create aliased test input");
    Seed(output);
    Require(!ReadAndConsume(input.c_str(), output) && Cleared(output), "hardlinked input rejected");
    Preserved(input, valid.data(), static_cast<DWORD>(valid.size()));
    Require(DeleteFileW(alias.c_str()) && DeleteFileW(input.c_str()), "remove aliased test input");
    Write(input, valid.data(), static_cast<DWORD>(valid.size()));
    Require(SetFileAttributesW(input.c_str(), FILE_ATTRIBUTE_READONLY) != 0, "make deletion-denied test input");
    Seed(output);
    Require(!ReadAndConsume(input.c_str(), output) && Cleared(output), "deletion failure prevents credential submission");
    Preserved(input, valid.data(), static_cast<DWORD>(valid.size()));
    Require(SetFileAttributesW(input.c_str(), FILE_ATTRIBUTE_NORMAL) && DeleteFileW(input.c_str()), "remove deletion-denied input");
    Require(CreateDirectoryW(input.c_str(), nullptr) != 0, "create directory-shaped input");
    Seed(output);
    Require(!ReadAndConsume(input.c_str(), output) && Cleared(output), "directory input rejected");
    Require(RemoveDirectoryW(input.c_str()) != 0, "remove directory-shaped input");
    Write(source, valid.data(), static_cast<DWORD>(valid.size()));
    // Symlink creation can require a runner privilege. When supported, exercise
    // the real reparse-point rejection; all other consumer cases are mandatory.
    if (CreateSymbolicLinkW(input.c_str(), source.c_str(), 2) || CreateSymbolicLinkW(input.c_str(), source.c_str(), 0)) {
        Seed(output);
        Require(!ReadAndConsume(input.c_str(), output) && Cleared(output), "reparse-point input rejected");
        Preserved(source, valid.data(), static_cast<DWORD>(valid.size()));
        Require(DeleteFileW(input.c_str()) != 0, "remove reparse-point input");
    } else {
        std::puts("SKIP: symlink creation unavailable; remaining private-file checks required");
    }
    Require(DeleteFileW(source.c_str()) != 0, "remove reparse-point source");
}
#endif
}

int main() {
    try {
        ParserTests();
        AttemptTests();
#if defined(_WIN32)
        ConsumerTests();
#endif
        std::puts("PASS: bounded local account parser, offline/readiness gates and once-only login handoff");
        return 0;
    } catch (const std::exception& error) {
        // Reasons are controlled test labels; no input bytes are printed.
        std::fprintf(stderr, "FAIL: local account handoff: %s\n", error.what());
        return 1;
    }
}
