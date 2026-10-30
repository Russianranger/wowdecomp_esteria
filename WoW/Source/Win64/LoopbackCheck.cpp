#include <winsock2.h>
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <memory>
#include <stdexcept>
#include <vector>
#include "Engine/Source/Base/CDataStore.h"
#include "Engine/Source/Base/CmdLine.h"
#include "Engine/Source/Platform/LocalOffline.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/Storm/H/storm.h"
#include "WoW/Source/ObjectAlloc/ObjectAlloc.h"
#include "WoW/Source/WowServices/WowConnection.h"
#include "WoW/Source/WowServices/WowConnectionNet.h"
#include "WoW/Source/WowServices/WDataStore.h"
#include "WoW/Source/Win64/LoopbackCheck.h"

namespace esteria::loopbackcheck {
namespace {
constexpr DWORD timeout = 6000;
const char* phase = "startup";
void Phase(const char* value) {
    phase = value;
    std::fprintf(stderr, "LOOPBACK_PHASE %s\n", phase);std::fflush(stderr);
}
std::atomic<unsigned> workerStarts{0};
HANDLE blockedWorkerEvent = nullptr;
std::atomic<bool> blockedWorkerEntered{false};
void WorkerStarted() { ++workerStarts; }
void BlockedWorkerStarted() {
    blockedWorkerEntered = true;
    WaitForSingleObject(blockedWorkerEvent, 30000);
}
void Require(bool condition, const char* reason) { if (!condition) throw std::runtime_error(reason); }
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value) CloseHandle(value); }
    Handle() = default;
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
struct Socket {
    SOCKET value = INVALID_SOCKET;
    ~Socket() { if (value != INVALID_SOCKET) closesocket(value); }
};
DWORD WINAPI Deadline(void* done) {
    if (WaitForSingleObject(done, 90000) != WAIT_OBJECT_0) {
        std::fputs("FAIL: client loopback diagnostic watchdog expired\n", stderr);
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
std::vector<uint8_t> Payload(size_t size, unsigned seed) {
    std::vector<uint8_t> result(size);
    for (size_t i = 0; i < size; ++i) result[i] = static_cast<uint8_t>(i * 37u + (i >> 8) * 11u + seed);
    return result;
}
std::vector<uint8_t> Frame(const std::vector<uint8_t>& payload) {
    const size_t count = payload.size();
    std::vector<uint8_t> wire;
    if (count > 0x7fff) wire.push_back(static_cast<uint8_t>((count >> 16) | 0x80));
    wire.push_back(static_cast<uint8_t>(count >> 8));wire.push_back(static_cast<uint8_t>(count));
    wire.insert(wire.end(), payload.begin(), payload.end());return wire;
}
void Ready(SOCKET socket, bool write) {
    fd_set descriptors;FD_ZERO(&descriptors);FD_SET(socket, &descriptors);
    timeval limit{static_cast<long>(timeout / 1000), 0};
    const int result = select(0, write ? nullptr : &descriptors, write ? &descriptors : nullptr, nullptr, &limit);
    if (result != 1) {
        std::fprintf(stderr, "LOOPBACK_READY phase=%s operation=%s result=%d error=%d\n",
            phase, write ? "write" : "read", result, result == SOCKET_ERROR ? WSAGetLastError() : 0);
        throw std::runtime_error("bounded loopback socket readiness");
    }
}
void SendAll(SOCKET socket, const uint8_t* bytes, size_t count) {
    const ULONGLONG end = GetTickCount64() + timeout;
    while (count) {
        Require(GetTickCount64() < end, "loopback send deadline");Ready(socket, true);
        const int sent = send(socket, reinterpret_cast<const char*>(bytes), static_cast<int>(count), 0);
        Require(sent > 0, "loopback peer send");bytes += sent;count -= sent;
    }
}
void ReceiveExact(SOCKET socket, const std::vector<uint8_t>& expected) {
    std::vector<uint8_t> actual(expected.size());size_t offset = 0;
    const ULONGLONG end = GetTickCount64() + timeout;
    while (offset < actual.size()) {
        Require(GetTickCount64() < end, "loopback receive deadline");Ready(socket, false);
        const int got = recv(socket, reinterpret_cast<char*>(actual.data() + offset), static_cast<int>(actual.size() - offset), 0);
        Require(got > 0, "loopback peer receive");offset += got;
    }
    Require(actual == expected, "actual client outbound bytes/frame differ");
}
struct Response final : WowConnectionResponse {
    const DWORD mainThread = GetCurrentThreadId();
    std::atomic<unsigned> connected{0}, disconnected{0}, failed{0}, packets{0}, streamBytes{0};
    std::atomic<bool> invalid{false}, offMainThread{true};
    std::vector<std::vector<uint8_t>> expected;
    std::vector<uint8_t> stream;
    void Thread() { if (GetCurrentThreadId() == mainThread) offMainThread = false; }
    void WCConnected(WowConnection*, WowConnection* inbound, uint32_t, const NETCONNADDR* address) override {
        Thread();const auto* peer = reinterpret_cast<const sockaddr_in*>(&address->peerAddr);
        if (inbound || peer->sin_family != AF_INET || peer->sin_addr.s_addr != htonl(INADDR_LOOPBACK)) invalid = true;
        ++connected;
    }
    void WCDisconnected(WowConnection*, uint32_t, const NETCONNADDR*) override { Thread();++disconnected; }
    void WCCantConnect(WowConnection*, uint32_t, const NETCONNADDR*) override { Thread();++failed; }
    void WCMessageReady(WowConnection*, uint32_t, CDataStore* message) override {
        Thread();const unsigned index = packets.load();
        if (index >= expected.size() || message->m_size != expected[index].size()) { invalid = true;return; }
        void* bytes = nullptr;message->GetDataInSitu(bytes, message->m_size);
        if (!bytes || std::memcmp(bytes, expected[index].data(), message->m_size)) invalid = true;
        ++packets;
    }
    void WCDataReady(WowConnection*, uint32_t, uint8_t* data, int32_t length) override {
        Thread();const unsigned offset = streamBytes.load();
        if (length <= 0 || offset + static_cast<unsigned>(length) > stream.size() ||
            std::memcmp(data, stream.data() + offset, length)) { invalid = true;return; }
        streamBytes += length;
    }
};
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
struct Connection {
    std::unique_ptr<WowConnection> value;
    explicit Connection(Response& response) : value(new WowConnection(&response, nullptr)) { }
    void Finish() {
        if (!value) return;
        value->SetResponse(nullptr, false);
        value->DoDisconnect();
        Until([this] {
            value->m_lock.Enter();const bool idle = value->m_refCount.m_value == 1 && !value->m_responseRef &&
                !value->m_serviceCount && value->m_sock < 0 && !value->m_netLink.IsLinked();value->m_lock.Leave();return idle;
        }, "connection callbacks/worker refs released before destruction");
        value.reset();
    }
    ~Connection() { try { Finish(); } catch (...) { TerminateProcess(GetCurrentProcess(), 70); } }
};
void Case(bool streamMode) {
    Phase(streamMode ? "stream-connect" : "message-connect");
    Response response;response.expected = {Payload(17, 13), Payload(32768, 79)};response.stream = Payload(1027, 163);
    Connection connection(response);auto* client = connection.value.get();
    if (streamMode) client->SetType(WOWC_TYPE_STREAM);
    Socket listener, peer;listener.value = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    Require(listener.value != INVALID_SOCKET, "owned loopback listener");
    sockaddr_in address{};address.sin_family = AF_INET;address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    Require(bind(listener.value, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0 &&
        listen(listener.value, 1) == 0, "bind/listen only on 127.0.0.1 ephemeral port");
    int addressSize = sizeof(address);
    Require(getsockname(listener.value, reinterpret_cast<sockaddr*>(&address), &addressSize) == 0 && address.sin_port != 0,
        "actual loopback listener address");
    // Exercise both client host+port and endpoint parsing through the enabled policy.
    char endpoint[48];std::snprintf(endpoint, sizeof(endpoint), "127.0.0.1:%u", static_cast<unsigned>(ntohs(address.sin_port)));
    Require(streamMode ? client->Connect(endpoint, timeout) : client->Connect("127.0.0.1", ntohs(address.sin_port), timeout),
        "local offline policy accepts its real loopback peer");
    Ready(listener.value, false);peer.value = accept(listener.value, nullptr, nullptr);
    Require(peer.value != INVALID_SOCKET, "accept actual client connection");
    BOOL noDelay = TRUE;DWORD deadline = timeout;
    Require(setsockopt(peer.value, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay)) == 0 &&
        setsockopt(peer.value, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&deadline), sizeof(deadline)) == 0 &&
        setsockopt(peer.value, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&deadline), sizeof(deadline)) == 0,
        "bounded peer I/O");
    Until([&] { return response.connected.load() == 1 || response.failed.load() || response.invalid.load(); }, "actual connected callback");
    Require(response.connected == 1 && response.failed == 0 && !response.invalid, "one worker-thread loopback connect");
    if (streamMode) {
        Phase("stream-receive-partial");
        SendAll(peer.value, response.stream.data(), 19);
        Until([&] { return response.streamBytes.load() == 19 || response.invalid.load(); }, "partial stream callback");
        Phase("stream-receive-rest");
        SendAll(peer.value, response.stream.data() + 19, response.stream.size() - 19);
        Until([&] { return response.streamBytes.load() == response.stream.size() || response.invalid.load(); }, "complete raw stream callbacks");
        Phase("stream-send");auto reply = Payload(1031, 211);
        Require(client->Send(reply.data(), static_cast<int32_t>(reply.size()), true) != WC_SEND_ERROR, "actual stream send");
        ReceiveExact(peer.value, reply);
        Require(response.packets == 0, "stream path must not manufacture framed callbacks");
    } else {
        for (unsigned i = 0; i < response.expected.size(); ++i) {
            Phase(i ? "message-receive-extended" : "message-receive-short");
            const auto wire = Frame(response.expected[i]);const size_t prefix = i + 1;
            SendAll(peer.value, wire.data(), prefix);
            Until([&] {
                client->m_lock.Enter();const bool held = client->m_readBytes == static_cast<int32_t>(prefix);client->m_lock.Leave();return held;
            }, "real parser retains a partial two/three-byte frame header");
            SendAll(peer.value, wire.data() + prefix, wire.size() - prefix);
            Until([&] { return response.packets.load() == i + 1 || response.invalid.load(); }, "actual framed message callback");
        }
        client->m_lock.Enter();const bool grew = client->m_readBufferSize >= 32771;client->m_lock.Leave();
        Require(grew, "actual receive buffer grew for extended frame");
        // Force the existing queued packet path with its real one-millisecond
        // latency setting; the network worker must drain the WDataStore nodes.
        WowConnection::s_lagTestDelayMin = 1;WowConnection::s_lagTestDelayMax = 1;
        for (const auto& incoming : response.expected) {
            Phase(incoming.size() > 0x7fff ? "message-queued-send-extended" : "message-queued-send-short");
            auto reply = Payload(incoming.size(), 233);CDataStore packet(reply.data(), static_cast<uint32_t>(reply.size()));
            Require(client->SendPacket(&packet, 0) == WC_SEND_QUEUED, "actual queued SendPacket path");
            ReceiveExact(peer.value, Frame(reply));
        }
        WowConnection::s_lagTestDelayMin = 0;WowConnection::s_lagTestDelayMax = 0;
        Require(response.streamBytes == 0, "message path must not manufacture stream callbacks");
    }
    Require(!response.invalid && response.failed == 0 && response.offMainThread, "exact worker-thread payload callbacks");
    Phase(streamMode ? "stream-disconnect" : "message-disconnect");
    client->Disconnect();Until([&] { return response.disconnected.load() == 1; }, "actual controlled disconnect callback");
    Require(response.connected == 1 && response.disconnected == 1, "one connect/disconnect lifecycle");
    connection.Finish();
}
void StopRetention() {
    Phase("stop-timeout-retention-and-retry");
    Require(WowConnection::s_network == nullptr, "retention case starts without a network");
    Handle barrier, worker, main;barrier.value = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Require(barrier.value != nullptr, "owned blocked-worker barrier");blockedWorkerEvent = barrier.value;
    struct Cleanup {
        ~Cleanup() {
            SetEvent(blockedWorkerEvent);
            WowConnection::DestroyOsNet();
            if (WowConnection::s_network) TerminateProcess(GetCurrentProcess(), 70);
            ObjectAllocDestroy();blockedWorkerEvent = nullptr;
        }
    } cleanup;
    const ULONGLONG started = GetTickCount64();
    Require(WowConnection::InitOsNet(nullptr, BlockedWorkerStarted, 1, false) != 0, "retention network starts");
    auto* retained = WowConnection::s_network;
    Require(retained && retained->m_event && DuplicateHandle(GetCurrentProcess(), retained->m_workers[0].m_thread.m_opaqueData,
        GetCurrentProcess(), &worker.value, SYNCHRONIZE | THREAD_QUERY_INFORMATION, FALSE, 0) &&
        DuplicateHandle(GetCurrentProcess(), retained->m_thread.m_opaqueData, GetCurrentProcess(), &main.value,
        SYNCHRONIZE | THREAD_QUERY_INFORMATION, FALSE, 0), "actual retained worker identities");
    Until([] { return blockedWorkerEntered.load(); }, "actual blocked worker entered");
    WowConnection::DestroyOsNet(); // Actual ten-second Stop timeout; must retain the live thread's dependencies.
    Require(GetTickCount64() - started < 25000 && WowConnection::s_network == retained &&
        WaitForSingleObject(worker.value, 0) == WAIT_TIMEOUT && WaitForSingleObject(main.value, 0) == WAIT_OBJECT_0,
        "timed-out worker must retain its exact network before its own barrier timeout");
    DWORD flags = 0;Require(GetHandleInformation(retained->m_event, &flags) != 0, "retained platform event remains live");
    { WDataStore pooled;pooled.Put(static_cast<uint32_t>(0x1234abcd));pooled.Finalize();uint32_t value = 0;pooled.Get(value);
      Require(pooled.m_size == 4 && value == 0x1234abcd, "pooled buffers remain usable after retained shutdown"); }
    SetEvent(barrier.value);
    Require(WaitForSingleObject(worker.value, timeout) == WAIT_OBJECT_0, "blocked worker exits after explicit release");
    DWORD exit = STILL_ACTIVE;Require(GetExitCodeThread(worker.value, &exit) && exit == 0, "released worker normal exit");
    WowConnection::DestroyOsNet();Require(WowConnection::s_network == nullptr, "shutdown retry releases retained network");
}
int Receipt() {
    wchar_t path[MAX_PATH] = {};const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* name = length && length < MAX_PATH ? std::wcsrchr(path, L'\\') : nullptr;
    constexpr wchar_t filename[] = L"Esteria-loopback-check.json";
    Require(name && MAX_PATH - static_cast<size_t>(++name - path) >= sizeof(filename) / sizeof(*filename), "bounded receipt path");
    std::wmemcpy(name, filename, sizeof(filename) / sizeof(*filename));
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return 66;
#if defined(_M_ARM64)
    constexpr char architecture[] = "ARM64";
#else
    constexpr char architecture[] = "x64";
#endif
    char data[2048];const int size = std::snprintf(data, sizeof(data),
        "{\"schemaVersion\":1,\"scope\":\"executable-loopback-transport-only\",\"architecture\":\"%s\",\"protocolBuild\":12340,"
        "\"checks\":{\"actualCommandParser\":true,\"localOfflineEnabled\":true,\"twoWorkerThreads\":true,\"loopbackConnectCallbacks\":true,"
        "\"partialFrameHeaders\":true,\"shortAndExtendedFrameBytes\":true,\"queuedPacketSendBytes\":true,\"streamReadAndSendBytes\":true,"
        "\"controlledDisconnectCallbacks\":true,\"connectionRefsReleased\":true,\"networkThreadsJoined\":true,\"stopTimeoutRetainsNetwork\":true},"
        "\"connections\":2,\"receivedFrames\":2,\"sentFrames\":2,\"receivedStreamBytes\":1027,\"sentStreamBytes\":1031,"
        "\"commonMainStarted\":false,\"assetsLoaded\":false,\"authenticationQualified\":false,\"gameplayQualified\":false}\n", architecture);
    DWORD written = 0;const bool complete = size > 0 && static_cast<size_t>(size) < sizeof(data) &&
        WriteFile(file, data, static_cast<DWORD>(size), &written, nullptr) && written == static_cast<DWORD>(size) && FlushFileBuffers(file);
    const bool closed = CloseHandle(file) != 0;
    if (!complete || !closed) { DeleteFileW(path);throw std::runtime_error("complete loopback receipt persistence"); }
    std::puts("PASS: Esteria actual network workers, loopback frames/stream and joined teardown (no authentication or game initialization)");return 0;
}
}
int Run() {
    try {
        Watchdog watchdog;
        const _ARGLIST diagnostic[] = {{SCMD_TYPE_BOOL, 45, "-loopback-check", nullptr}};
        Require(SCmdRegisterArgList(diagnostic, 1) && IsThreadAFiber() && CmdLineProcess() && Esteria::LocalOffline::Enabled(),
            "actual local offline command parser on main fiber");
        struct Timer {
            Timer() { OsTimeManagerCreate(OsTimeManager::BestAvailable); }
            ~Timer() { OsTimeManagerDestroy(); }
        } timer;
        { Network network;Case(false);Case(true);network.Finish(); }
        StopRetention();
        return Receipt();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: Esteria loopback transport check: %s (phase=%s)\n", error.what(), phase);return 65;
    }
}
}
