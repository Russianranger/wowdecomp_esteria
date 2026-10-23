// Host test aid: a client-only way to put an M3 on the local player without server or DBC changes.
#include "UnitWorldBinding.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include <string>
namespace ASECM3::Client {
namespace {
std::string playerModel;
// ASECM3_SetPlayerModel("Path\model.m3") or nil to restore the real model.
int SetPlayerModel(lua_State* L){
    if(lua_isnoneornil(L,1))playerModel.clear();
    else{const char* path=luaL_checkstring(L,1);if(!*path)playerModel.clear();else playerModel=path;}
    if(auto* player=ClntObjMgrGetActivePlayerObj())player->UpdateDisplayInfo(1);
    lua_pushboolean(L,1);return 1;
}
}
const char* PlayerModelOverride(){return playerModel.empty()?nullptr:playerModel.c_str();}
void RegisterPlayerModelScriptFunctions(){FrameScript_RegisterFunction("ASECM3_SetPlayerModel",SetPlayerModel);}
}
