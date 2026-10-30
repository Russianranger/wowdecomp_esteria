#if defined(WOWDECOMP_ARCH_64)

#include <windows.h>
#include <cstdlib>
#include <float.h>
#include "Engine/Source/Storm/H/storm.h"
#include "WoW/Source/Client.h"
#include "WoW/Source/Win64/EntryCheck.h"

// STATIC: 0x00B311E8
static LPVOID lpFiber;
static LPVOID entryCheckFiber;
static bool entryCheckRequested;

static void InvalidParameterHandler(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, uintptr_t reserved) {
}

static bool ReturnFalse() {
    return false;
}

// Esteria: same export the Win32 Client.dll provides, so launcher tooling can probe the client protocol level.
extern "C" __declspec(dllexport) int EsteriaClientVersion() {
    return 4;
}

extern "C" int WinMainCRTStartup();

// OFFSET: 0x00401000
extern "C" int start() {
    StormRtlInitialize();
    return WinMainCRTStartup();
}

// OFFSET: 0x00406D70
int main() {
    _set_invalid_parameter_handler(InvalidParameterHandler);
    _clearfp();
    _control87(0x9001F & ~(_MCW_PC | _MCW_IC), _MCW_DN | _MCW_EM | _MCW_RC);
    ReturnFalse();

    if (entryCheckRequested)
        return esteria::entrycheck::OnMainFiber(lpFiber, entryCheckFiber);

    return CommonMain();
}

// OFFSET: 0x0040B7D0
static VOID CALLBACK MainFiberProc(LPVOID parameter) {
    *static_cast<int*>(parameter) = main();
    SwitchToFiber(lpFiber);
}

// OFFSET: 0x0040B7F0
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    const auto entryMode = esteria::entrycheck::ParseCommandLine();
    if (entryMode == esteria::entrycheck::Mode::Invalid) return 64;
    entryCheckRequested = entryMode == esteria::entrycheck::Mode::Requested;
    lpFiber = ConvertThreadToFiber(nullptr);

    if (!lpFiber) {
        return -1;
    }

    int result = -1;
    LPVOID fiber = CreateFiberEx(0, 0x800000, 0, MainFiberProc, &result);

    if (!fiber) {
        return -1;
    }

    if (entryCheckRequested) entryCheckFiber = fiber;
    SwitchToFiber(fiber);
    DeleteFiber(fiber);

    if (entryCheckRequested)
        return esteria::entrycheck::FinishAfterFiberReturn(result, lpFiber);

    return result;
}

#endif
