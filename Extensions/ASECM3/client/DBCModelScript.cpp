#include "UnitWorldBinding.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include <cmath>
#include <cctype>
#include <cstring>
#include <exception>
namespace ASECM3::Client {
namespace {
int Failure(lua_State* L,const char* text){lua_pushnil(L);lua_pushstring(L,text);return 2;}
int Info(lua_State* L){
    if(lua_type(L,1)!=LUA_TNUMBER)return Failure(L,"DBC display ID must be a positive integer");
    const double input=lua_tonumber(L,1);
    if(!std::isfinite(input)||std::floor(input)!=input||input<=0||input>INT32_MAX)return Failure(L,"DBC display ID must be a positive int32");
    if(g_creatureDisplayInfoDB.m_loaded<=0||g_creatureModelDataDB.m_loaded<=0||g_creatureDisplayInfoDB.m_numRecords<=0||g_creatureModelDataDB.m_numRecords<=0)return Failure(L,"Model DBC tables are not loaded");
    try{
        const auto* display=g_creatureDisplayInfoDB.GetRecord(int32_t(input));
        if(!display||display->m_id!=int32_t(input))return Failure(L,"Display ID was not found in the loaded CreatureDisplayInfo table");
        if(display->m_modelID<=0)return Failure(L,"Display record has no valid model ID");
        const auto* model=g_creatureModelDataDB.GetRecord(display->m_modelID);
        if(!model||model->m_id!=display->m_modelID)return Failure(L,"Display record points to a missing CreatureModelData record");
        if(!model->m_modelName)return Failure(L,"Model record has no path");
        const auto length=strnlen_s(model->m_modelName,4097);
        if(!length||length>4096)return Failure(L,"Model DBC path is empty or exceeds 4096 bytes");
        if(!std::isfinite(model->m_modelScale)||!std::isfinite(display->m_creatureModelScale))return Failure(L,"Model DBC scale is nonfinite");
        std::string format="other";std::string path(model->m_modelName,length);
        const auto dot=path.find_last_of('.');if(dot!=std::string::npos){auto suffix=path.substr(dot+1);for(auto& c:suffix)c=char(std::tolower(static_cast<unsigned char>(c)));if(suffix=="m3"||suffix=="m2"||suffix=="mdx")format=std::move(suffix);}
        const bool exists=SFile::FileExists(path.c_str())!=0;
        lua_newtable(L);
        const auto number=[&](const char* name,double value){lua_pushnumber(L,value);lua_setfield(L,-2,name);};
        number("displayId",display->m_id);number("modelId",model->m_id);number("displayScale",display->m_creatureModelScale);number("modelScale",model->m_modelScale);number("alpha",display->m_creatureModelAlpha);
        lua_pushlstring(L,path.data(),path.size());lua_setfield(L,-2,"path");lua_pushstring(L,format.c_str());lua_setfield(L,-2,"format");lua_pushboolean(L,exists);lua_setfield(L,-2,"exactPathExists");lua_pushboolean(L,format=="m3");lua_setfield(L,-2,"isM3");
        number("displayRecords",g_creatureDisplayInfoDB.m_numRecords);number("modelRecords",g_creatureModelDataDB.m_numRecords);return 1;
    }catch(const std::exception& error){return Failure(L,error.what());}
}
}
void RegisterDBCModelScriptFunctions(){FrameScript_RegisterFunction("ASECM3_GetDBCModelInfo",Info);}
}
