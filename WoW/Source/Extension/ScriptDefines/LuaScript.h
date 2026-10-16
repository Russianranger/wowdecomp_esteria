#ifndef WOW_SOURCE_EXTENSION_SCRIPTDEFINES_LUASCRIPT_H
#define WOW_SOURCE_EXTENSION_SCRIPTDEFINES_LUASCRIPT_H

#include <cstdint>
#include "WoW/Source/Extension/ScriptObject.h"

struct lua_State;

// Which FrameScript state is being set up. The glue (login, realm list, character screens) and the
// game UI run in different Lua states with different function tables and event arrays.
enum FrameScriptContext {
    FRAMESCRIPT_CONTEXT_GLUE = 0,
    FRAMESCRIPT_CONTEXT_GAME = 1,
};

// The FrameScript (Lua) state. The client destroys and recreates its Lua state several times:
// CGlueMgr::Resume rebuilds the glue state every time the glue comes up (first start, every return
// from the game, glue reload), CGGameUI::InitializeFrameXML rebuilds the game state on every world
// entry and on /reloadui. Each rebuild starts from an empty state, so everything a script
// registered in the previous state is gone: functions, tables, references and event indices.
// OnFrameScriptInitialized therefore fires at every rebuild and a script must redo its whole
// registration in it, every time.
//
// Events are addressed by index into one array that FrameScript_CreateEvents replaces wholesale.
// A script never calls FrameScript_CreateEvents itself (that would discard the engine's events);
// it declares its event names through ExtensionMgr::DeclareFrameScriptEvent inside this hook, and
// the core creates the engine's names and the declared names together in the one call.
class LuaScript : public ScriptObject {
    protected:
    explicit LuaScript(const char* name);

    public:
    // The state L is created and the engine's own function table for this context is registered
    // (CGlueMgr::Resume after CGVideoOptions::RegisterScriptFunctions, CGGameUI::InitializeFrameXML
    // after LoadScriptFunctions). The event array has not been created yet: this is the only place
    // ExtensionMgr::DeclareFrameScriptEvent may be called. Register functions with
    // FrameScript_RegisterFunction (they run as engine code, untainted) and build Lua tables with
    // the lua_* API on L. No FrameXML or addon file has been loaded yet in this state.
    virtual void OnFrameScriptInitialized(lua_State* L, FrameScriptContext context) {}

    // Start of FrameScript_Destroy (Engine/Source/FrameScript/FrameScript.cpp), before
    // lua_close: the state L is about to go, with every function, table, reference and event
    // registered in it. Fires for each rebuild (the Destroy half of CGlueMgr::Resume's flush,
    // CGGameUI::InitializeFrameXML, /reloadui) and once at client shutdown. Drop anything that
    // points into the state here; the next OnFrameScriptInitialized gets a fresh one.
    virtual void OnFrameScriptDestroy(lua_State* L) {}
};

#endif
