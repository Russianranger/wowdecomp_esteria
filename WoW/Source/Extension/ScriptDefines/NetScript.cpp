#include "WoW/Source/Extension/ScriptDefines/NetScript.h"
#include "Engine/Source/Base/CDataStore.h"
#include "WoW/Source/Extension/ExtensionMgr.h"
#include "WoW/Source/Extension/ExtensionMgrMacros.h"

void ExtensionMgr::OnMessageReceived(NETMESSAGE msgId, uint32_t timeReceived, CDataStore* msg) {
    if (ScriptRegistry<NetScript>::ScriptPointerList.empty()) {
        return;
    }

    // The cursor is at the payload. Each script gets it there, and the handler the client
    // dispatches to afterwards gets it there too, whatever the scripts read.
    uint32_t read = msg->m_read;

    CALL_HOOKS(NetScript, (msg->m_read = read, script->OnMessageReceived(msgId, timeReceived, *msg)));

    msg->m_read = read;
}

void ExtensionMgr::OnMessageSent(CDataStore* msg) {
    if (ScriptRegistry<NetScript>::ScriptPointerList.empty()) {
        return;
    }

    // Outgoing messages start with the opcode as a 32-bit value (every sender does
    // msg.Put(static_cast<uint32_t>(CMSG_...))); the connection serialises it from the cursor.
    uint32_t read = msg->m_read;
    uint32_t opcode = 0;

    if (msg->m_size - read >= sizeof(opcode)) {
        msg->Get(opcode);
    }

    CALL_HOOKS(NetScript, (msg->m_read = read, script->OnMessageSent(static_cast<NETMESSAGE>(opcode), *msg)));

    msg->m_read = read;
}

NetScript::NetScript(const char* name) : ScriptObject(name) {
    ScriptRegistry<NetScript>::AddScript(this);
    sExtensionMgr->Log("Extensions: registered NetScript %s", name);
}
