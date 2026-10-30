#include <winsock2.h>
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <memory>
#include <stdexcept>
#include <string>
#include "Engine/Source/Base/CmdLine.h"
#include "Engine/Source/Platform/LocalOffline.h"
#include "Engine/Source/Os/System.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/Storm/H/storm.h"
#include "WoW/Source/ObjectAlloc/ObjectAlloc.h"
#include "WoW/Source/WowServices/Grunt.h"
#include "WoW/Source/WowServices/WowConnectionNet.h"
#include "WoW/Source/Win64/AuthCheck.h"

// Opt-in owned-fixture diagnostic. Secrets enter/leave only through private
// sibling files; the public receipt contains no account name or session key.
namespace esteria::authcheck {
namespace {
constexpr DWORD timeout = 15000;
std::atomic<unsigned> workerStarts{0};
void WorkerStarted() { ++workerStarts; }
void Require(bool condition, const char* reason) { if (!condition) throw std::runtime_error(reason); }
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value) CloseHandle(value); }
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
DWORD WINAPI Deadline(void* done) {
    if (WaitForSingleObject(done, 90000) != WAIT_OBJECT_0) {
        std::fputs("FAIL: client auth diagnostic watchdog expired\n", stderr);
        std::fflush(stderr);
        TerminateProcess(GetCurrentProcess(), 70);
    }
    return 0;
}
struct Watchdog {
    Handle done, thread;
    Watchdog() {
        done.value = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        Require(done.value != nullptr, "watchdog event");
        thread.value = CreateThread(nullptr, 0, Deadline, done.value, 0, nullptr);
        Require(thread.value != nullptr, "watchdog thread");
    }
    ~Watchdog() {
        SetEvent(done.value);
        if (WaitForSingleObject(thread.value, 5000) != WAIT_OBJECT_0) TerminateProcess(GetCurrentProcess(), 70);
    }
};
template<class Predicate> void Until(Predicate condition, const char* reason) {
    const ULONGLONG end = GetTickCount64() + timeout;
    while (!condition()) { Require(GetTickCount64() < end, reason); Sleep(2); }
}
struct Network {
    Handle threads[3];bool finished = false;
    Network() {
        Require(WowConnection::s_network == nullptr, "diagnostic starts without a network instance");
        try {
            Require(WowConnection::InitOsNet(nullptr, WorkerStarted, 2, false) != 0, "actual network initialization");
            auto* network = WowConnection::s_network;
            Require(network && network->m_event && network->m_numWorkers == 2, "actual network and worker handles");
            HANDLE original[3] = {network->m_thread.m_opaqueData, network->m_workers[0].m_thread.m_opaqueData,
                network->m_workers[1].m_thread.m_opaqueData};
            for (unsigned i = 0; i < 3; ++i) {
                Require(DuplicateHandle(GetCurrentProcess(), original[i], GetCurrentProcess(), &threads[i].value,
                    SYNCHRONIZE | THREAD_QUERY_INFORMATION, FALSE, 0) != 0, "retain actual worker identity");
                Require(WaitForSingleObject(threads[i].value, 0) == WAIT_TIMEOUT, "actual network thread must be live");
            }
            Until([] { return workerStarts.load() == 2; }, "two actual worker initializers");
        } catch (...) {
            WowConnection::DestroyOsNet();
            if (WowConnection::s_network) TerminateProcess(GetCurrentProcess(), 70);
            ObjectAllocDestroy();throw;
        }
    }
    void Finish() {
        WowConnection::DestroyOsNet();
        // Retained live workers still need the timer and pools. Fail the owned
        // diagnostic process instead of unwinding those dependencies below them.
        if (WowConnection::s_network) TerminateProcess(GetCurrentProcess(), 70);
        finished = true;
        Require(WowConnection::s_network == nullptr, "network instance released");
        for (auto& thread : threads) {
            DWORD code = STILL_ACTIVE;
            Require(WaitForSingleObject(thread.value, 0) == WAIT_OBJECT_0 && GetExitCodeThread(thread.value, &code) && code == 0,
                "actual network/worker threads joined normally");
        }
        ObjectAllocDestroy();
    }
    ~Network() {
        if (!finished) {
            WowConnection::DestroyOsNet();
            if (WowConnection::s_network) TerminateProcess(GetCurrentProcess(), 70);
            ObjectAllocDestroy();
        }
    }
};
std::wstring Sibling(const wchar_t* filename) {
    wchar_t path[MAX_PATH] = {};const DWORD size = GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* name = size && size < MAX_PATH ? std::wcsrchr(path, L'\\') : nullptr;
    Require(name && std::wcslen(filename) < MAX_PATH - static_cast<size_t>(++name - path), "bounded private fixture path");
    std::wcscpy(name, filename);return path;
}
bool Absent(const std::wstring& path) {
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return false;
    return GetLastError() == ERROR_FILE_NOT_FOUND;
}
struct Credentials {
    char username[18] = {}, password[17] = {}, fixture[33] = {};
    Credentials() {
        const auto path = Sibling(L"Esteria-auth-input.bin");
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        Require(file != INVALID_HANDLE_VALUE, "private credential fixture is unavailable");
        Handle owned;owned.value = file;BY_HANDLE_FILE_INFORMATION info{};
        Require(GetFileType(file) == FILE_TYPE_DISK && GetFileInformationByHandle(file, &info) &&
            !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
            info.nNumberOfLinks == 1 && !info.nFileSizeHigh && info.nFileSizeLow == 57,
            "private credential fixture shape differs");
        unsigned char raw[57] = {};DWORD read = 0;
        Require(ReadFile(file, raw, sizeof(raw), &read, nullptr) && read == sizeof(raw), "complete private fixture read");
        const bool magic = std::memcmp(raw, "ESTAUTH1", 8) == 0;
        for (unsigned i = 0; i < 16; ++i) std::snprintf(fixture + 2 * i, 3, "%02x", raw[8 + i]);
        std::memcpy(username, raw + 24, 17);std::memcpy(password, raw + 41, 16);
        SecureZeroMemory(raw, sizeof(raw));
        bool valid = magic && std::memcmp(username, "EST", 3) == 0;
        for (unsigned i = 3; i < 17; ++i) valid &= std::strchr("0123456789ABCDEF", username[i]) != nullptr && username[i] != 0;
        for (unsigned i = 0; i < 16; ++i) valid &= std::strchr("ABCDEFGHJKLMNPQRSTUVWXYZ23456789", password[i]) != nullptr && password[i] != 0;
        if (!valid) { SecureZeroMemory(password, sizeof(password));SecureZeroMemory(username, sizeof(username)); }
        Require(valid, "private fixture must contain an ordinary generated local account");
    }
    ~Credentials() { SecureZeroMemory(password, sizeof(password));SecureZeroMemory(username, sizeof(username)); }
};
constexpr uint8_t versionChallenge[16] = {0xba,0xa3,0x1e,0x99,0xa0,0x0b,0x21,0x57,0xfc,0x37,0x3f,0xb3,0x69,0xcd,0xd2,0xf1};
struct Response final : Grunt::ClientResponse {
    Credentials& credentials;Grunt::ClientLink* link = nullptr;
    std::atomic<bool> invalid{false};
    std::atomic<unsigned> connected{0}, disconnected{0}, logonRequests{0}, challenges{0}, authenticated{0}, realms{0}, security{0};
    uint8_t key[40] = {};uint8_t realmFlags = 0;bool realmBuildPresent = false;
    explicit Response(Credentials& input) : credentials(input) { }
    ~Response() { SecureZeroMemory(key, sizeof(key)); }
    bool Connected(const NETADDR& address) override {
        const auto* peer = reinterpret_cast<const sockaddr_in*>(&address);
        if (++connected != 1 || peer->sin_family != AF_INET || peer->sin_addr.s_addr != htonl(INADDR_LOOPBACK) ||
            ntohs(peer->sin_port) != 3724) invalid = true;
        return !invalid;
    }
    bool Disconnected() override { ++disconnected;return false; } // The fixture owns ClientLink.
    bool CantConnect() override { invalid = true;return false; }
    bool OnlineIdle() override { return !invalid; }
    void GetLogonMethod() override {
        if (++logonRequests != 1 || invalid) { invalid = true;return; }
        Grunt::ClientLink::Logon logon{};logon.m_accountName = credentials.username;logon.m_password = credentials.password;
        logon.m_programId = 0x576f57;logon.m_processor = OsGetProcessorId();logon.m_os = OsGetOsId();
        logon.m_versionMajor = 3;logon.m_versionMinor = 3;logon.m_versionPatch = 5;
        logon.m_build = 12340;logon.m_locale = 0x656e5553;logon.m_timeZone = 0;
        link->LogonNewSession(logon);
    }
    void GetVersionProof(const uint8_t* challenge) override {
        if (++challenges != 1 || invalid || !challenge || std::memcmp(challenge, versionChallenge, sizeof(versionChallenge))) {
            invalid = true;return;
        }
        // The matching auth fixture explicitly verifies source-default
        // StrictVersionCheck=0. No game-derived version checksum is fabricated.
        const uint8_t unusedChecksum[20] = {};link->ProveVersion(unusedChecksum);
    }
    void SetPinInfo(bool enabled, uint32_t, const uint8_t*) override { if (enabled) invalid = true;++security; }
    void GetPinInfo(bool* enabled, uint8_t** salt, uint8_t** hash) override { *enabled = false;*salt = nullptr;*hash = nullptr; }
    void SetMatrixInfo(bool enabled, uint8_t, uint8_t, uint8_t, uint8_t, bool, uint8_t, uint64_t, const uint8_t*, uint32_t) override {
        if (enabled) invalid = true;++security;
    }
    void GetMatrixProof(bool* enabled, const uint8_t** proof) const override { *enabled = false;*proof = nullptr; }
    void SetTokenInfo(bool enabled, uint8_t) override { if (enabled) invalid = true;++security; }
    void GetTokenInfo(bool* enabled, char* token) override { *enabled = false;token[0] = 0; }
    bool GetCdKeys(Grunt::CdKey& keys) override { keys.m_count = 0;return false; }
    void LogonResult(Grunt::Result result, const uint8_t* value, uint32_t size, uint16_t) override {
        if (result != Grunt::GRUNT_RESULT_SUCCESS || !value || size != sizeof(key) || authenticated.load() || invalid) {
            invalid = true;return;
        }
        std::memcpy(key, value, sizeof(key));++authenticated;
    }
    void RealmListResult(CDataStore* message) override {
        try {
            Require(message && message->m_data && message->m_read <= message->m_size && message->m_read >= message->m_base &&
                message->m_size - message->m_read <= 1024, "bounded actual realm-list callback");
            const uint8_t* bytes = message->m_data + message->m_read - message->m_base;
            const size_t size = message->m_size - message->m_read;size_t offset = 0;
            const auto take = [&](size_t count) { Require(count <= size - offset, "bounded realm field");const auto* value = bytes + offset;offset += count;return value; };
            const auto string = [&](size_t maximum) {
                const size_t begin = offset;
                while (offset < size && bytes[offset] && offset - begin < maximum) ++offset;
                Require(offset < size && bytes[offset] == 0, "bounded realm string");
                std::string value(reinterpret_cast<const char*>(bytes + begin), offset - begin);++offset;return value;
            };
            const auto* count = take(2);Require(count[0] == 1 && count[1] == 0, "exactly one advertised realm");
            const auto* flags = take(3);Require(flags[1] == 0, "local realm is unlocked");realmFlags = flags[2];
            Require(!string(32).empty() && string(255) == "127.0.0.1:8085", "advertised local world endpoint");
            const auto* populationBytes = take(4);float population;std::memcpy(&population, populationBytes, sizeof(population));
            Require(std::isfinite(population) && population >= 0, "finite realm population");
            const auto* tail = take(3);Require(tail[2] == 1, "local realm identifier");
            realmBuildPresent = (realmFlags & 4) != 0;
            if (realmBuildPresent) {
                const auto* build = take(5);Require(build[0] == 3 && build[1] == 3 && build[2] == 5 &&
                    (build[3] | (build[4] << 8)) == 12340, "advertised realm build12340");
            }
            const auto* trailer = take(2);Require(trailer[0] == 0x10 && trailer[1] == 0 && offset == size, "complete realm-list trailer");
            if (++realms != 1) invalid = true;
        } catch (...) { invalid = true; }
    }
    void ReconnectResult(Grunt::Result, const uint8_t*, uint32_t, uint16_t) override { invalid = true; }
    void GetFileStatus(const char*, uint64_t, const uint8_t*) override { invalid = true; }
    void FileData(const uint8_t*, uint32_t) override { invalid = true; }
    LOGIN_STATE NextSecurityState(LOGIN_STATE) override { invalid = true;return LOGIN_STATE_FAILED; }
    int32_t GetServerId() override { return 0; }
    const uint8_t* GetVersionChallenge() override { return versionChallenge; }
};
struct Session {
    Response& response;std::unique_ptr<Grunt::ClientLink> link;WowConnection* connection = nullptr;Handle timer;
    bool finished = false;
    explicit Session(Response& target) : response(target), link(new Grunt::ClientLink(target)) {
        response.link = link.get();
        Require(link->m_timer.m_thread.Valid() && DuplicateHandle(GetCurrentProcess(), link->m_timer.m_thread.m_opaqueData,
            GetCurrentProcess(), &timer.value, SYNCHRONIZE | THREAD_QUERY_INFORMATION, FALSE, 0), "retain actual Grunt timer thread");
        Require(WaitForSingleObject(timer.value, 0) == WAIT_TIMEOUT, "Grunt timer is live");
    }
    void Connect() {
        link->m_lock.Enter();link->Connect("127.0.0.1:3724");connection = link->m_connection;
        if (connection) connection->AddRef();link->m_lock.Leave();
        Require(connection != nullptr, "retain real Grunt connection before callbacks");
    }
    void Finish() {
        if (finished) return;
        // Join the timer and drain response refs outside ClientLink's lock, then
        // release its owning ref before the diagnostic's final retained ref.
        link->m_timer.Shutdown();
        if (connection) connection->SetResponse(nullptr, false);
        link->Shutdown();
        if (connection) {
            connection->DoDisconnect();
            Until([&] {
                connection->m_lock.Enter();const bool idle = connection->m_refCount.m_value == 1 && !connection->m_responseRef &&
                    !connection->m_serviceCount && connection->m_sock < 0 && !connection->m_netLink.IsLinked();
                connection->m_lock.Leave();return idle;
            }, "Grunt connection callbacks and worker refs released");
            auto* final = connection;connection = nullptr;
            Require(final->Release() == 0, "final Grunt connection ref released");
        }
        link.reset();finished = true;DWORD code = STILL_ACTIVE;
        Require(WaitForSingleObject(timer.value, 0) == WAIT_OBJECT_0 && GetExitCodeThread(timer.value, &code) && code == 1,
            "real Grunt timer joined normally");
    }
    ~Session() { try { Finish(); } catch (...) { TerminateProcess(GetCurrentProcess(), 70); } }
};
void WriteExclusive(const std::wstring& path, const void* bytes, DWORD size) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    Require(file != INVALID_HANDLE_VALUE, "exclusive diagnostic output creation");DWORD written = 0;
    const bool complete = WriteFile(file, bytes, size, &written, nullptr) && written == size && FlushFileBuffers(file);
    const bool closed = CloseHandle(file) != 0;
    if (!complete || !closed) { DeleteFileW(path.c_str());throw std::runtime_error("complete diagnostic output persistence"); }
}
void Receipt(const Credentials& credentials, const Response& response, const std::wstring& receiptPath, const std::wstring& keyPath) {
#if defined(_M_ARM64)
    constexpr char architecture[] = "ARM64";
#else
    constexpr char architecture[] = "x64";
#endif
    char data[2048];const int size = std::snprintf(data, sizeof(data),
        "{\"schemaVersion\":1,\"scope\":\"executable-grunt-authentication-only\",\"architecture\":\"%s\",\"protocolBuild\":12340,\"fixtureId\":\"%s\","
        "\"checks\":{\"actualCommandParser\":true,\"localOfflineEnabled\":true,\"twoWorkerThreads\":true,\"gruntTimerThread\":true,"
        "\"connectedCallback\":true,\"newSessionRequest\":true,\"versionChallengeObserved\":true,\"serverProofVerified\":true,"
        "\"realmListCallback\":true,\"controlledDisconnect\":true,\"connectionRefsReleased\":true,\"gruntTimerJoined\":true,\"networkThreadsJoined\":true},"
        "\"authentications\":1,\"realms\":1,\"realmId\":1,\"worldEndpoint\":\"127.0.0.1:8085\",\"realmFlags\":%u,\"realmBuildPresent\":%s,"
        "\"privateSessionKeyBytes\":40,\"versionChecksumQualified\":false,\"commonMainStarted\":false,\"assetsLoaded\":false,"
        "\"compiledServerQualified\":false,\"worldEntryQualified\":false,\"gameplayQualified\":false}\n",
        architecture, credentials.fixture, response.realmFlags, response.realmBuildPresent ? "true" : "false");
    Require(size > 0 && static_cast<size_t>(size) < sizeof(data), "bounded public auth receipt");
    WriteExclusive(keyPath, response.key, sizeof(response.key));
    try { WriteExclusive(receiptPath, data, static_cast<DWORD>(size)); }
    catch (...) { DeleteFileW(keyPath.c_str());throw; }
}
}
int Run() {
    try {
        Watchdog watchdog;
        const _ARGLIST diagnostic[] = {{SCMD_TYPE_BOOL, 46, "-auth-check", nullptr}};
        Require(SCmdRegisterArgList(diagnostic, 1) && IsThreadAFiber() && CmdLineProcess() && Esteria::LocalOffline::Enabled(),
            "actual local offline auth parser on main fiber");
        const auto receipt = Sibling(L"Esteria-auth-check.json"), key = Sibling(L"Esteria-auth-session.key");
        if (!Absent(receipt) || !Absent(key)) return 66;
        Credentials credentials;Response response(credentials);
        struct Timer { Timer() { OsTimeManagerCreate(OsTimeManager::BestAvailable); } ~Timer() { OsTimeManagerDestroy(); } } timer;
        {
            Network network;
            {
                Session session(response);session.Connect();
                Until([&] { return response.authenticated.load() || response.invalid.load(); }, "actual Grunt authentication deadline");
                Require(!response.invalid && response.authenticated == 1 && response.connected == 1 && response.logonRequests == 1 &&
                    response.challenges == 1 && response.security == 3, "actual Grunt/SRP proof and callbacks");
                session.link->m_lock.Enter();session.link->GetRealmList();session.link->m_lock.Leave();
                Until([&] { return response.realms.load() || response.invalid.load(); }, "actual realm-list callback deadline");
                Require(!response.invalid && response.realms == 1, "actual local realm list");
                session.link->Disconnect();
                Until([&] { return response.disconnected.load() == 1 || response.invalid.load(); }, "actual Grunt disconnect deadline");
                Require(!response.invalid && response.disconnected == 1, "one controlled Grunt disconnect");session.Finish();
                Require(!response.invalid && response.connected == 1 && response.disconnected == 1 && response.authenticated == 1 &&
                    response.realms == 1 && response.logonRequests == 1 && response.challenges == 1 && response.security == 3,
                    "final callback counts after joined Grunt teardown");
            }
            network.Finish();
        }
        Receipt(credentials, response, receipt, key);
        std::puts("PASS: Esteria actual Grunt/SRP callbacks, local realm and joined teardown (no game initialization)");return 0;
    } catch (const std::exception& error) {
        // All diagnostic exceptions use controlled text, never account/key data.
        std::fprintf(stderr, "FAIL: Esteria Grunt authentication check: %s\n", error.what());return 65;
    }
}
}
