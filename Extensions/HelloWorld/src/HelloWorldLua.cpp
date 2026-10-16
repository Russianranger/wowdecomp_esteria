#include "Engine/Source/FrameScript/FrameScript.h"
#include "WoW/Source/Extension/ExtensionMgr.h"

namespace {

// The index of HELLO_WORLD_PING in the current FrameScript state's event array. It is renewed at
// every OnFrameScriptInitialized, because every rebuild of the state gets a new array.
uint32_t s_pingEventIndex = UINT32_MAX;
uint32_t s_pingCount;

// HelloWorld_Ping(): a global the extension adds to both Lua states. It signals HELLO_WORLD_PING
// with the call count to every frame that registered for it, and returns the count. From the UI:
//   /run local f = CreateFrame("Frame") f:RegisterEvent("HELLO_WORLD_PING") f:SetScript("OnEvent", function(_, e, n) print(e, n) end)
//   /run print(HelloWorld_Ping())
int32_t Script_HelloWorld_Ping(lua_State* L) {
    s_pingCount++;

    if (s_pingEventIndex != UINT32_MAX) {
        FrameScript_SignalEvent(s_pingEventIndex, "%d", s_pingCount);
    }

    lua_pushnumber(L, s_pingCount);

    return 1;
}

class HelloWorldLuaScript : public LuaScript {
    public:
    HelloWorldLuaScript() : LuaScript("HelloWorldLuaScript") {}

    void OnFrameScriptInitialized(lua_State* L, FrameScriptContext context) override {
        FrameScript_RegisterFunction("HelloWorld_Ping", Script_HelloWorld_Ping);
        s_pingEventIndex = sExtensionMgr->DeclareFrameScriptEvent("HELLO_WORLD_PING");

        sExtensionMgr->Log("HelloWorld: OnFrameScriptInitialized (%s state), HELLO_WORLD_PING is event %u", context == FRAMESCRIPT_CONTEXT_GLUE ? "glue" : "game", s_pingEventIndex);
    }

    // The event index dies with the state; forget it so a ping between states signals nothing.
    void OnFrameScriptDestroy(lua_State* L) override {
        s_pingEventIndex = UINT32_MAX;
        sExtensionMgr->Log("HelloWorld: OnFrameScriptDestroy after %u ping(s)", s_pingCount);
    }
};

}

void AddHelloWorldLuaScripts() {
    new HelloWorldLuaScript();
}
