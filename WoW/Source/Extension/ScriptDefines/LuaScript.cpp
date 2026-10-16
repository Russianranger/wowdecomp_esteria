#include "WoW/Source/Extension/ScriptDefines/LuaScript.h"
#include <vector>
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Storm/H/storm.h"
#include "WoW/Source/Extension/ExtensionMgr.h"
#include "WoW/Source/Extension/ExtensionMgrMacros.h"

// The event array under construction, valid only while OnFrameScriptInitialized runs: the engine's
// names first (their indices are the engine's enum values and must not move), the declared names
// after them. The pointers are the callers' string literals, which outlive the array.
static std::vector<const char*> s_eventNames;
static bool s_declaring;

uint32_t ExtensionMgr::DeclareFrameScriptEvent(const char* name) {
    if (!s_declaring || !name) {
        Log("Extensions: DeclareFrameScriptEvent(%s) outside LuaScript::OnFrameScriptInitialized, ignored", name ? name : "null");
        return UINT32_MAX;
    }

    // FrameScript_CreateEvents maps equal names to one event object, so two indices for the same
    // name would both work; one index per name keeps FrameScript_SignalEvent callers consistent.
    for (uint32_t i = 0; i < s_eventNames.size(); i++) {
        if (s_eventNames[i] && !SStrCmpI(s_eventNames[i], name, STORM_MAX_STR)) {
            return i;
        }
    }

    s_eventNames.push_back(name);

    return static_cast<uint32_t>(s_eventNames.size() - 1);
}

void ExtensionMgr::OnFrameScriptInitialized(FrameScriptContext context, const char** names, uint32_t count) {
    s_eventNames.assign(names, names + count);
    s_declaring = true;

    lua_State* L = FrameScript_GetContext();
    CALL_HOOKS(LuaScript, script->OnFrameScriptInitialized(L, context));

    s_declaring = false;

    // One call for the engine's events and the declared ones: the array is replaced wholesale,
    // so the two sets have to arrive together.
    FrameScript_CreateEvents(s_eventNames.data(), static_cast<uint32_t>(s_eventNames.size()));

    if (s_eventNames.size() > count) {
        Log("Extensions: %u FrameScript event(s) declared in the %s state", static_cast<uint32_t>(s_eventNames.size() - count), context == FRAMESCRIPT_CONTEXT_GLUE ? "glue" : "game");
    }

    s_eventNames.clear();
}

LuaScript::LuaScript(const char* name) : ScriptObject(name) {
    ScriptRegistry<LuaScript>::AddScript(this);
    sExtensionMgr->Log("Extensions: registered LuaScript %s", name);
}
