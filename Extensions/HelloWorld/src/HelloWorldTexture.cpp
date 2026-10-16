#include "Engine/Source/Services/Texture.h"
#include "WoW/Source/Extension/ExtensionMgr.h"

namespace {

// Logs the first file textures created and counts the rest (cache hits never reach the hook).
class HelloWorldTextureScript : public TextureScript {
    public:
    HelloWorldTextureScript() : TextureScript("HelloWorldTextureScript") {}

    void OnTextureCreated(CTexture& texture, const char* fileName) override {
        m_created++;

        if (m_created <= 5) {
            sExtensionMgr->Log("HelloWorld: OnTextureCreated %s", fileName);
        } else if (m_created % 500 == 0) {
            sExtensionMgr->Log("HelloWorld: %u file texture(s) created", m_created);
        }
    }

    private:
    uint32_t m_created = 0;
};

}

void AddHelloWorldTextureScripts() {
    new HelloWorldTextureScript();
}
