#include "Engine/Source/Base/CDataStore.h"
#include "WoW/Source/Extension/ExtensionMgr.h"

namespace {

// Logs the first few messages in each direction with their opcode and payload size, then only
// counts. The sizes read the stream's own fields; nothing is consumed from it.
class HelloWorldNetScript : public NetScript {
    public:
    HelloWorldNetScript() : NetScript("HelloWorldNetScript") {}

    void OnConnected(NetClient& client) override {
        sExtensionMgr->Log("HelloWorld: OnConnected");
    }

    void OnDisconnected(NetClient& client) override {
        sExtensionMgr->Log("HelloWorld: OnDisconnected after %u received, %u sent", m_received, m_sent);
    }

    // Lets everything through; a real script would compare msgId against the opcodes it owns.
    bool CanMessageReceive(NETMESSAGE msgId, uint32_t timeReceived, CDataStore& msg) override {
        return true;
    }

    void OnMessageReceived(NETMESSAGE msgId, uint32_t timeReceived, CDataStore& msg) override {
        m_received++;

        if (m_received <= 5) {
            sExtensionMgr->Log("HelloWorld: OnMessageReceived opcode 0x%03X, %u payload byte(s), t=%u", msgId, msg.m_size - msg.m_read, timeReceived);
        } else if (m_received % 500 == 0) {
            sExtensionMgr->Log("HelloWorld: %u message(s) received, %u sent", m_received, m_sent);
        }
    }

    void OnMessageSent(NETMESSAGE msgId, CDataStore& msg) override {
        m_sent++;

        if (m_sent <= 5) {
            sExtensionMgr->Log("HelloWorld: OnMessageSent opcode 0x%03X, %u byte(s)", msgId, msg.m_size - msg.m_read);
        }
    }

    private:
    uint32_t m_received = 0;
    uint32_t m_sent = 0;
};

}

void AddHelloWorldNetScripts() {
    new HelloWorldNetScript();
}
