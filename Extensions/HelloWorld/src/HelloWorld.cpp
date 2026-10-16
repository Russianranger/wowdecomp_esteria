#include "WoW/Source/Extension/ExtensionMgr.h"

namespace {

class HelloWorldScript : public ClientScript
{
    public:
    HelloWorldScript() : ClientScript("HelloWorldScript") {}

    void OnStartup() override
    {
        ExtensionMgr::Log("HelloWorld: OnStartup");
    }

    void OnShutdown() override
    {
        ExtensionMgr::Log("HelloWorld: OnShutdown");
    }

    void OnEnterWorld(uint32_t mapId) override
    {
        ExtensionMgr::Log("HelloWorld: OnEnterWorld (map %u)", mapId);
    }

    void OnLeaveWorld() override
    {
        ExtensionMgr::Log("HelloWorld: OnLeaveWorld");
    }
};

}

void AddHelloWorldExtension()
{
    ExtensionMgr::Add(new HelloWorldScript());
}