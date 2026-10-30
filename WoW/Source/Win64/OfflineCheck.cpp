#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include "Engine/Source/Base/CmdLine.h"
#include "Engine/Source/BlizzardCore/Source/Packages/Streaming/Tracker.h"
#include "Engine/Source/BlizzardCore/Source/System/URL/URL.h"
#include "Engine/Source/Os/OsURLDownload.h"
#include "Engine/Source/Platform/LocalOffline.h"
#include "Engine/Source/Storm/H/storm.h"
#include "WoW/Source/WowServices/WowConnection.h"
#include "WoW/Source/Win64/OfflineCheck.h"

namespace esteria::offlinecheck {
namespace {
unsigned osCallbacks = 0;
bool OsCallback(void*, const char*, uint32_t, bool, HTTPError::ErrorCode) { ++osCallbacks; return false; }
struct UrlCallback : Blizzard::URL::DownloadCallback {
    unsigned calls = 0;
    bool ReceivedData(const void*, int32_t) override { ++calls; return false; }
    void Completed(Status) override { ++calls; }
};
struct ConnectionResponse : WowConnectionResponse {
    unsigned failures = 0, unexpected = 0;
    void WCMessageReady(WowConnection*, uint32_t, CDataStore*) override { ++unexpected; }
    void WCConnected(WowConnection*, WowConnection*, uint32_t, const NETCONNADDR*) override { ++unexpected; }
    void WCDisconnected(WowConnection*, uint32_t, const NETCONNADDR*) override { ++unexpected; }
    void WCCantConnect(WowConnection* connection, uint32_t, const NETCONNADDR*) override {
        ++failures;
        // Exercise same-thread response removal used by Grunt failure cleanup.
        connection->SetResponse(nullptr, false);
    }
};
int Fail(const char* reason) {
    std::fprintf(stderr, "FAIL: Esteria local-offline boundary check: %s\n", reason);
    return 65;
}
int Receipt() {
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* name = length && length < MAX_PATH ? std::wcsrchr(path, L'\\') : nullptr;
    constexpr wchar_t receiptName[] = L"Esteria-local-offline-check.json";
    if (!name || MAX_PATH - static_cast<size_t>(++name - path) < sizeof(receiptName) / sizeof(*receiptName))
        return Fail("bounded receipt path");
    std::wmemcpy(name, receiptName, sizeof(receiptName) / sizeof(*receiptName));
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return 66;
#if defined(_M_ARM64)
    constexpr char architecture[] = "ARM64";
#else
    constexpr char architecture[] = "x64";
#endif
    char data[2048];
    const int size = std::snprintf(data, sizeof(data),
        "{\"schemaVersion\":1,\"scope\":\"executable-local-offline-boundaries-only\",\"architecture\":\"%s\","
        "\"protocolBuild\":12340,\"checks\":{\"actualCommandParser\":true,\"osUrlRejected\":true,"
        "\"blizzardUrlRejected\":true,\"initiationFailureCallbacksPreserved\":true,\"trackerReturned\":true,"
        "\"hostConnectionRejected\":true,\"numericConnectionRejected\":true,\"invalidPortRejected\":true,"
        "\"connectFailureCallbacks\":true,\"connectionRefsReleased\":true},"
        "\"remoteAttemptsRejected\":3,\"failureCallbacks\":3,\"commonMainStarted\":false,\"assetsLoaded\":false,"
        "\"loopbackGameplayQualified\":false}\n", architecture);
    DWORD written = 0;
    const bool complete = size > 0 && static_cast<size_t>(size) < sizeof(data) &&
        WriteFile(file, data, static_cast<DWORD>(size), &written, nullptr) && written == static_cast<DWORD>(size) &&
        FlushFileBuffers(file);
    const bool closed = CloseHandle(file) != 0;
    if (!complete || !closed) { DeleteFileW(path); return Fail("complete receipt persistence"); }
    std::puts("PASS: Esteria actual command parser and local-offline network boundaries (no game initialization)");
    return 0;
}
}

int Run() {
    // This diagnostic flag is registered only in its explicitly selected path.
    // ID44 is separate from both normal offline options (42/43).
    const _ARGLIST diagnostic[] = {{SCMD_TYPE_BOOL, 44, "-offline-check", nullptr}};
    if (!SCmdRegisterArgList(diagnostic, 1)) return Fail("diagnostic SCmd registration");
    if (!IsThreadAFiber() || !CmdLineProcess() || !Esteria::LocalOffline::Enabled()) return Fail("actual SCmd offline option");
    constexpr char url[] = "https://example.invalid/esteria-offline-fixture";
    UrlCallback callback;
    if (OsURLDownload(url, OsCallback, nullptr) || Blizzard::URL::DownloadURL(url, &callback) || osCallbacks || callback.calls)
        return Fail("real HTTP initiation refusal without callbacks");
    const char* previousTracker = Tracker::trackerAddress;
    Tracker::trackerAddress = url;
    const ULONGLONG began = GetTickCount64();
    Tracker::SendToTracker("fixture", 0, 0, "");
    Tracker::trackerAddress = previousTracker;
    if (GetTickCount64() - began > 1000) return Fail("tracker must not wait on an unstarted request");
    ConnectionResponse response;
    WowConnection connection(&response, nullptr);
    if (WowConnection::s_network != nullptr) return Fail("diagnostic must not initialize a network worker");
    if (connection.Connect("example.invalid", 3724, 1)) return Fail("host connection was accepted");
    connection.SetResponse(&response, false);
    const unsigned char remote[4] = {203, 0, 113, 1};
    uint32_t address;std::memcpy(&address, remote, sizeof(address));
    if (connection.Connect(address, 8085, 1)) return Fail("numeric connection was accepted");
    connection.SetResponse(&response, false);
    if (connection.Connect("127.0.0.1:65536", 1)) return Fail("invalid port was accepted");
    if (response.failures != 3 || response.unexpected || connection.m_responseRef ||
        connection.m_refCount.m_value != 1 || connection.m_sock >= 0 || connection.m_connState != WOWC_DISCONNECTED)
        return Fail("failure callback or connection ref lifecycle");
    return Receipt();
}
}
