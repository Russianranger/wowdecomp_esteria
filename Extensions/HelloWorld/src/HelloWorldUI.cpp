#include <new>
#include <typeinfo>
#include "Engine/Source/Base/CDataAllocator.h"
#include "Engine/Source/Base/Status.h"
#include "Engine/Source/Frame/CSimpleFrame.h"
#include "Engine/Source/FrameScript/FrameScript_Object.h"
#include "Engine/Source/FrameXML/XMLTree.h"
#include "Engine/Source/Storm/H/storm.h"
#include "WoW/Source/Extension/ExtensionMgr.h"

namespace {

// A new UI element type, <HelloWorldFrame>, usable from XML and from CreateFrame("HelloWorldFrame").
// It is a Frame with one extra attribute, greeting="...", and its own object type so Lua's
// GetObjectType() / IsObjectType("HelloWorldFrame") recognise it. It inherits the Frame script
// methods (no metatable of its own). Frames are destroyed through the virtual destructor and the
// static operator delete of their class, so the type owns its allocator like CGCooldown does.
class CHelloWorldFrame : public CSimpleFrame {
    public:
    static CDataAllocator s_allocator;
    static int32_t s_objectType;
    static const char* s_objectTypeName;

    static void operator delete(void* ptr) {
        if (ptr) {
            CHelloWorldFrame::s_allocator.PutData(ptr, nullptr, 0);
        }
    }

    static CSimpleFrame* Create(CSimpleFrame* parent) {
        void* data = CHelloWorldFrame::s_allocator.GetData(0, typeid(CHelloWorldFrame).raw_name(), SLOG_OBJECT);
        return data ? new (data) CHelloWorldFrame(parent) : nullptr;
    }

    static int32_t GetObjectType() {
        if (!CHelloWorldFrame::s_objectType) {
            CHelloWorldFrame::s_objectType = ++FrameScript_Object::s_objectTypes;
        }

        return CHelloWorldFrame::s_objectType;
    }

    explicit CHelloWorldFrame(CSimpleFrame* parent) : CSimpleFrame(parent) {}

    bool IsA(int32_t type) override {
        return type == CHelloWorldFrame::GetObjectType() || CSimpleFrame::IsA(type);
    }

    bool IsA(const char* typeName) override {
        return !SStrCmpI(typeName, CHelloWorldFrame::s_objectTypeName, STORM_MAX_STR) || CSimpleFrame::IsA(typeName);
    }

    const char* GetObjectTypeName() override {
        return CHelloWorldFrame::s_objectTypeName;
    }

    // The Frame attributes and children first, then the type's own attribute.
    void LoadXML(XMLNode* node, CStatus* status) override {
        CSimpleFrame::LoadXML(node, status);

        const char* greeting = node->GetAttributeByName("greeting");
        const char* name = node->GetAttributeByName("name");
        sExtensionMgr->Log("HelloWorld: <HelloWorldFrame> %s created, greeting=\"%s\"", name && *name ? name : "(unnamed)", greeting ? greeting : "");
    }
};

CDataAllocator CHelloWorldFrame::s_allocator(sizeof(CHelloWorldFrame), 16);
int32_t CHelloWorldFrame::s_objectType;
const char* CHelloWorldFrame::s_objectTypeName = "HelloWorldFrame";

// Counts every frame the UI builds and reports element types nothing knows.
class HelloWorldUIScript : public UIScript {
    public:
    HelloWorldUIScript() : UIScript("HelloWorldUIScript") {}

    void OnFrameCreated(CSimpleFrame& frame, XMLNode& node) override {
        m_created++;

        if (m_created <= 3 || m_created % 1000 == 0) {
            sExtensionMgr->Log("HelloWorld: OnFrameCreated <%s> (%u frame(s) so far)", node.m_name.m_str, m_created);
        }
    }

    void OnUnknownFrameType(const char* name, XMLNode& node) override {
        sExtensionMgr->Log("HelloWorld: OnUnknownFrameType <%s>", name);
    }

    private:
    uint32_t m_created = 0;
};

}

void AddHelloWorldUIScripts() {
    sExtensionMgr->RegisterXmlElement("HelloWorldFrame", CHelloWorldFrame::Create);
    new HelloWorldUIScript();
}
