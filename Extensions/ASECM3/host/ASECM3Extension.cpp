// Host adapter: connects the ASECM3 module to the client through extension scripts. The few hooks an
// extension cannot reach (model name substitution, element queue, draw dispatch, bounds) are direct calls
// in Unit_C.cpp, M2Scene.cpp, CM2SceneRender.cpp and M2Model.cpp.
#include "Engine/Source/Os/TimeManager.h"
#include "Extensions/ASECM3/client/UnitWorldBinding.h"
#include "WoW/Source/Extension/ExtensionMgr.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"

namespace {

class ASECM3ObjectScript : public ObjectScript {
    public:
    ASECM3ObjectScript() : ObjectScript("ASECM3ObjectScript") {}

    void OnObjectDestroy(CGObject_C& object) override {
        if (object.m_obj->m_type & TYPEMASK_UNIT) {
            ASECM3::Client::RemoveUnitModel(static_cast<CGUnit_C*>(&object));
        }
    }
};

class ASECM3RenderScript : public RenderScript {
    public:
    ASECM3RenderScript() : RenderScript("ASECM3RenderScript") {}

    void OnFrameBegin(float) override { ASECM3::Client::UpdateWorldBindings(OsGetAsyncTimeMs()); }
};

class ASECM3WorldScript : public WorldScript {
    public:
    ASECM3WorldScript() : WorldScript("ASECM3WorldScript") {}

    void OnMapUnload() override { ASECM3::Client::ResetWorldBindings(); }
};

class ASECM3LuaScript : public LuaScript {
    public:
    ASECM3LuaScript() : LuaScript("ASECM3LuaScript") {}

    void OnFrameScriptInitialized(lua_State*, FrameScriptContext context) override {
        if (context == FRAMESCRIPT_CONTEXT_GAME) {
            ASECM3::Client::RegisterWorldEffectScriptFunctions();
            ASECM3::Client::RegisterDBCModelScriptFunctions();
            ASECM3::Client::RegisterPlayerModelScriptFunctions();
        }
    }
};

}

void AddASECM3Extension() {
    new ASECM3ObjectScript();
    new ASECM3RenderScript();
    new ASECM3WorldScript();
    new ASECM3LuaScript();
}
