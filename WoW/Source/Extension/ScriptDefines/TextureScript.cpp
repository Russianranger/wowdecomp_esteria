#include "WoW/Source/Extension/ScriptDefines/TextureScript.h"
#include "WoW/Source/Extension/ExtensionMgr.h"
#include "WoW/Source/Extension/ExtensionMgrMacros.h"

void ExtensionMgr::OnTextureCreated(CTexture* texture, const char* fileName) {
    CALL_HOOKS(TextureScript, script->OnTextureCreated(*texture, fileName));
}

TextureScript::TextureScript(const char* name) : ScriptObject(name) {
    ScriptRegistry<TextureScript>::AddScript(this);
    sExtensionMgr->Log("Extensions: registered TextureScript %s", name);
}
