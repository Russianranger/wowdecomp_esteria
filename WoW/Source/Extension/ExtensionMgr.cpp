#include "WoW/Source/Extension/ExtensionMgr.h"
#include <cstdarg>
#include "Engine/Source/ConsoleBase/IConsole.h"
#include "Engine/Source/Storm/H/storm.h"
#include "WoW/Source/Extension/ExtensionLoader.h"

static HSLOG s_log;
static bool s_initialized;

void ExtensionMgr::Log(const char* format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    SStrVPrintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (s_log) {
        SLogWrite(s_log, "%s", buffer);
        SLogFlush(s_log);
    }

    ConsolePrintf("%s", buffer);
}

void ExtensionMgr::Add(ClientScript* script)
{
    if (!script)
    {
        return;
    }

    ExtensionRegistry<ClientScript>::Add(script);
    Log("Extensions: registered ClientScript %s", script->GetName());
}

uint32_t ExtensionMgr::GetScriptCount()
{
    return static_cast<uint32_t>(ExtensionRegistry<ClientScript>::Scripts().size());
}

void ExtensionMgr::Initialize()
{
    if (s_initialized)
    {
        return;
    }

    s_initialized = true;

    if (!SLogCreate("Logs\\Extensions.log", SLOG_FLAG_OPENFILE, &s_log))
    {
        s_log = nullptr;
    }

    Log("Extensions: %u compiled in", g_extensionCount);

    for (const EXTENSIONENTRY* entry = g_extensionEntries; entry->m_name; entry++)
    {
        Log("Extensions: loading %s", entry->m_name);
        entry->m_add();
    }

    Log("Extensions: %u script(s) registered", GetScriptCount());

    for (ClientScript* script : ExtensionRegistry<ClientScript>::Scripts())
    {
        script->OnStartup();
    }
}

void ExtensionMgr::Shutdown()
{
    if (!s_initialized)
    {
        return;
    }

    for (ClientScript* script : ExtensionRegistry<ClientScript>::Scripts())
    {
        script->OnShutdown();
    }

    Log("Extensions: shutdown");
    ExtensionRegistry<ClientScript>::Clear();

    if (s_log)
    {
        SLogClose(s_log);
        s_log = nullptr;
    }

    s_initialized = false;
}

void ExtensionMgr::OnEnterWorld(uint32_t mapId)
{
    for (ClientScript* script : ExtensionRegistry<ClientScript>::Scripts()) {
        script->OnEnterWorld(mapId);
    }
}

void ExtensionMgr::OnLeaveWorld()
{
    for (ClientScript* script : ExtensionRegistry<ClientScript>::Scripts()) {
        script->OnLeaveWorld();
    }
}