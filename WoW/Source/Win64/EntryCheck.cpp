#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cwchar>
#include <float.h>
#include "Engine/Source/Storm/H/storm.h"
#include "WoW/Source/Win64/EntryCheck.h"

extern "C" int EsteriaClientVersion();

namespace esteria::entrycheck {
namespace {
constexpr wchar_t kFlag[] = L"--esteria-entry-check";
constexpr wchar_t kReceipt[] = L"Esteria-entry-check.json";
bool mainFiberPassed = false;

int Fail(const char* message, int code = 65) {
    std::fprintf(stderr, "FAIL: Esteria entry-check only: %s (Win32 error %lu)\n", message, GetLastError());
    std::fflush(stderr);
    return code;
}

bool CheckAllocator() {
    auto* original = static_cast<unsigned char*>(SMemAlloc(4096, __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY));
    if (!original) return false;
    for (unsigned i = 0; i < 4096; ++i) {
        if (original[i] != 0) { SMemFree(original, __FILE__, __LINE__, 0); return false; }
        original[i] = static_cast<unsigned char>(i * 37u + 19u);
    }
    auto* resized = static_cast<unsigned char*>(SMemReAlloc(original, 8192, __FILE__, __LINE__, 0));
    if (!resized) { SMemFree(original, __FILE__, __LINE__, 0); return false; }
    bool preserved = true;
    for (unsigned i = 0; i < 4096; ++i)
        if (resized[i] != static_cast<unsigned char>(i * 37u + 19u)) preserved = false;
    for (unsigned i = 4096; i < 8192; ++i) resized[i] = static_cast<unsigned char>(i * 11u + 7u);
    for (unsigned i = 4096; i < 8192; ++i)
        if (resized[i] != static_cast<unsigned char>(i * 11u + 7u)) preserved = false;
    return SMemFree(resized, __FILE__, __LINE__, 0) != 0 && preserved;
}

struct ProbeException { DWORD value; };
struct UnwindProbe {
    unsigned& destroyed;
    ~UnwindProbe() { ++destroyed; }
};

// Keep a genuine cross-function C++ unwind rather than an optimizable inline
// conditional. The exception is caught; no fake crash success is reported.
__declspec(noinline) void ThrowProbe(unsigned& destroyed, DWORD value) {
    UnwindProbe guard{destroyed};
    throw ProbeException{value};
}

bool CheckUnwind() {
    unsigned destroyed = 0;
    const DWORD value = GetCurrentThreadId() ^ 12340u;
    try {
        ThrowProbe(destroyed, value);
    } catch (const ProbeException& error) {
        return error.value == value && destroyed == 1;
    } catch (...) {
        return false;
    }
    return false;
}
}

Mode ParseCommandLine() {
    int count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return Mode::Invalid;
    Mode mode = Mode::None;
    for (int i = 1; i < count; ++i) {
        // The complete diagnostic flag prefix with wrong case, an appended
        // value or extra arguments must not fall through into normal gameplay.
        if (_wcsnicmp(arguments[i], kFlag, (sizeof(kFlag) / sizeof(*kFlag)) - 1) == 0) {
            mode = (count == 2 && std::wcscmp(arguments[i], kFlag) == 0)
                ? Mode::Requested : Mode::Invalid;
            break;
        }
    }
    LocalFree(arguments);
    return mode;
}

int OnMainFiber(void* callerFiber, void* expectedMainFiber) {
    mainFiberPassed = false;
    if (!callerFiber || !expectedMainFiber || !IsThreadAFiber()
        || GetCurrentFiber() != expectedMainFiber || GetCurrentFiber() == callerFiber)
        return Fail("did not execute on the newly-created main fiber");
    unsigned control = 0;
    if (_controlfp_s(&control, 0, 0) != 0) return Fail("could not read initialized CRT floating-point control");
    if (!CheckAllocator()) return Fail("real Storm/CRT allocation or reallocation failed");
    if (!CheckUnwind()) return Fail("caught C++ exception did not unwind its local destructor");
    if (EsteriaClientVersion() != 4) return Fail("unexpected Esteria extension version");
    mainFiberPassed = true;
    return 0;
}

int FinishAfterFiberReturn(int result, void* callerFiber) {
    if (result != 0) return result;
    if (!mainFiberPassed || !IsThreadAFiber() || GetCurrentFiber() != callerFiber)
        return Fail("main fiber did not return to its caller");

    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return Fail("executable path is unavailable or exceeds diagnostic bound");
    wchar_t* name = std::wcsrchr(path, L'\\');
    if (!name) return Fail("executable directory is unavailable");
    ++name;
    const size_t remaining = MAX_PATH - static_cast<size_t>(name - path);
    if (remaining < sizeof(kReceipt) / sizeof(*kReceipt)) return Fail("receipt path exceeds diagnostic bound");
    std::wmemcpy(name, kReceipt, sizeof(kReceipt) / sizeof(*kReceipt));

    // Never overwrite earlier evidence or any user file. The caller selects a
    // fresh binary directory for each attempt and removes it only after export.
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return Fail("could not exclusively create entry-only receipt", 66);
#if defined(_M_ARM64)
    constexpr char architecture[] = "ARM64";
#elif defined(_M_X64)
    constexpr char architecture[] = "x64";
#else
#error Unqualified 64-bit entry-check architecture
#endif
    char receipt[1024] = {};
    const int bytes = std::snprintf(receipt, sizeof(receipt),
        "{\"schemaVersion\":1,\"scope\":\"executable-entry-only\",\"architecture\":\"%s\","
        "\"protocolBuild\":12340,\"extensionVersion\":4,"
        "\"checks\":{\"imageEntryReached\":true,\"crtInitialized\":true,\"stormAllocator\":true,"
        "\"cppUnwind\":true,\"mainFiber\":true,\"fiberReturn\":true,\"fpControlRead\":true},"
        "\"commonMainStarted\":false,\"assetsLoaded\":false}\n", architecture);
    DWORD written = 0;
    const bool valid = bytes > 0 && static_cast<size_t>(bytes) < sizeof(receipt);
    const bool complete = valid && WriteFile(file, receipt, static_cast<DWORD>(bytes), &written, nullptr)
        && written == static_cast<DWORD>(bytes) && FlushFileBuffers(file);
    const bool closed = CloseHandle(file) != 0;
    if (!complete || !closed) {
        DeleteFileW(path); // This attempt exclusively created this incomplete file.
        return Fail("could not persist complete entry-only receipt", 67);
    }
    std::puts("PASS: Esteria executable entry/CRT/allocator/exception/fiber roundtrip (entry-check only)");
    std::fflush(stdout);
    return 0;
}
}
