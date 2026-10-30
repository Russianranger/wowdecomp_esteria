#include <winsock2.h>
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include "Engine/Source/Platform/PrivateLocalAccountFile.h"
#include "Engine/Source/Platform/LocalOffline.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "WoW/Source/Glue/CGlueMgr.h"
#include "WoW/Source/Glue/LocalAccountLogin.h"
#include "WoW/Source/Glue/Types.h"
#include "WoW/Source/WowSvcs/WowSvcsClient/ClientServices.h"

namespace Esteria::LocalLogin {
namespace {
bool framesLoaded = false;
Attempt attempt;
bool Consume(LocalAccount::Credentials& credentials) {
    constexpr wchar_t filename[] = L"Esteria-local-login.bin";
    wchar_t path[MAX_PATH] = {};
    const DWORD size = GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* name = size && size < MAX_PATH ? std::wcsrchr(path, L'\\') : nullptr;
    if (!name || sizeof(filename) / sizeof(*filename) > MAX_PATH - static_cast<std::size_t>(++name - path)) return false;
    std::wcscpy(name, filename);
    return LocalAccount::ReadAndConsume(path, credentials);
}
}
void SetFramesLoaded(bool loaded) { framesLoaded = loaded; }
void OnGlueIdle() {
    if (!Requested()) return;
    Readiness readiness;
    readiness.initialized = CGlueMgr::m_initialized != 0;
    readiness.framesLoaded = framesLoaded;
    readiness.suspended = CGlueMgr::m_suspended != 0;
    readiness.reloading = CGlueMgr::m_reload != 0;
    readiness.loginScreen = std::strcmp(CGlueMgr::m_currentScreen, "login") == 0;
    readiness.idle = CGlueMgr::m_idleState == IDLE_NONE;
    readiness.scanReady = CGlueMgr::m_scandllOkayToLogIn != 0;
    readiness.tosAccepted = CGlueMgr::m_acceptedTOS != 0;
    readiness.eulaAccepted = CGlueMgr::m_acceptedEULA != 0;
    readiness.clientServicesReady = ClientServices::s_currentConnection && ClientServices::s_clientRealmResponse;
    const auto result = attempt.Run(true, LocalOffline::Enabled(), readiness, Consume,
        [](const char* username, char* password) { CGlueMgr::LoginServerLogin(username, password); });
    if (result == Result::ReadFailed || result == Result::Rejected) {
        constexpr char message[] = "Esteria local login handoff is unavailable or invalid. Restart local play to try again.";
        std::fputs("ESTERIA_LOCAL_LOGIN refused: private handoff unavailable or invalid\n", stderr);
        FrameScript_SignalEvent(GLUE_EVENT_OPEN_STATUS_DIALOG, "%s%s", "OKAY", message);
    }
}
}
