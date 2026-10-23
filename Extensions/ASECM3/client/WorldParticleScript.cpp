#include "UnitWorldBinding.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include <cmath>
namespace ASECM3::Client {
namespace {
int Info(lua_State* L){
    const auto* player=ClntObjMgrGetActivePlayerObj();const auto info=GetUnitWorldInfo(player);lua_newtable(L);
    const auto number=[&](const char* field,double value){lua_pushnumber(L,value);lua_setfield(L,-2,field);};
    const auto text=[&](const char* field,const std::string& value){lua_pushstring(L,value.c_str());lua_setfield(L,-2,field);};
    lua_pushboolean(L,player!=nullptr);lua_setfield(L,-2,"inWorld");lua_pushboolean(L,info.ready);lua_setfield(L,-2,"ready");
    number("clip",info.effectClip);number("particles",double(info.effectParticles));number("spawned",double(info.effectSpawned));number("dropped",double(info.effectDropped));number("draws",double(info.effectDraws));
    number("birthTransformSamples",double(info.effectBirthSamples));number("materialRecords",info.effectMaterialRecords);text("materialError",info.effectMaterialError);text("bank",info.effectAnimationSource);text("dependencyError",info.effectError);text("simulationError",info.effectSimulationError);text("drawError",info.effectDrawError);
    lua_newtable(L);for(size_t i=0;i<info.effectClips.size();++i){lua_pushstring(L,info.effectClips[i].c_str());lua_rawseti(L,-2,int(i+1));}lua_setfield(L,-2,"clips");return 1;
}
int SetSequence(lua_State* L){
    const auto index=luaL_checknumber(L,1);std::string error;
    if(!std::isfinite(index)||std::floor(index)!=index||index<-1||index>INT32_MAX)error="M3 effect index must be -1 or a nonnegative source integer";
    else if(SetUnitEffectSequence(ClntObjMgrGetActivePlayerObj(),int32_t(index),error)){lua_pushboolean(L,1);return 1;}
    lua_pushnil(L);lua_pushstring(L,error.c_str());return 2;
}
}
void RegisterWorldEffectScriptFunctions(){RegisterDBCModelScriptFunctions();FrameScript_RegisterFunction("ASECM3_GetPlayerEffects",Info);FrameScript_RegisterFunction("ASECM3_SetPlayerEffectSequence",SetSequence);}
}
