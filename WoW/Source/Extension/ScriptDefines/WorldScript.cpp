#include "WoW/Source/Extension/ScriptDefines/WorldScript.h"
#include "WoW/Source/Extension/ExtensionMgr.h"
#include "WoW/Source/Extension/ExtensionMgrMacros.h"

void ExtensionMgr::OnMapLoaded(uint32_t mapId, const char* mapName) {
    CALL_HOOKS(WorldScript, script->OnMapLoaded(mapId, mapName));
}

void ExtensionMgr::OnMapUnload() {
    CALL_HOOKS(WorldScript, script->OnMapUnload());
}

void ExtensionMgr::OnWorldUpdate(float elapsedSec) {
    CALL_HOOKS(WorldScript, script->OnWorldUpdate(elapsedSec));
}

WorldScript::WorldScript(const char* name) : ScriptObject(name) {
    ScriptRegistry<WorldScript>::AddScript(this);
    sExtensionMgr->Log("Extensions: registered WorldScript %s", name);
}
