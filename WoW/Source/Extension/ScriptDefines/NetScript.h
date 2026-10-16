#ifndef WOW_SOURCE_EXTENSION_SCRIPTDEFINES_NETSCRIPT_H
#define WOW_SOURCE_EXTENSION_SCRIPTDEFINES_NETSCRIPT_H

#include <cstdint>
#include "WoW/Source/Extension/ScriptObject.h"
#include "WoW/Source/Net/Types.h"

class CDataStore;

// The world-server connection, at the two points every message passes through
// (WoW/Source/Net/NetClient/NetClient.cpp). Both hooks observe: they cannot drop or rewrite a
// message, and the core restores the stream's read cursor after the hooks so the engine's own
// handler sees the message exactly as if no script had looked at it. A script may therefore read
// from msg freely with CDataStore::Get. Do not keep a pointer to msg past the hook.
class NetScript : public ScriptObject {
    protected:
    explicit NetScript(const char* name);

    public:
    // NetClient::ProcessMessage, after the 16-bit opcode is read and before the message is
    // dispatched to the handler registered for it (or dropped if there is none). The read cursor
    // is at the payload, where a handler starts reading. Fires for every message of a connected
    // session, including opcodes nothing handles; msgId may be outside NETMESSAGE if the server
    // sent one. timeReceived is the receive timestamp the connection stamped the packet with.
    virtual void OnMessageReceived(NETMESSAGE msgId, uint32_t timeReceived, CDataStore& msg) {}

    // NetClient::Send, right before the message goes to the connection's SendPacket. The read
    // cursor is at the 32-bit opcode (what the connection serialises); msgId is that opcode. Fires
    // once per message actually handed to the connection: messages held while the client is
    // suspended (realm redirect) fire when the held queue is flushed, not when they were queued.
    virtual void OnMessageSent(NETMESSAGE msgId, CDataStore& msg) {}
};

#endif
