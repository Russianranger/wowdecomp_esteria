#include "Engine/Source/Model2/M2Data.h"
#include "Engine/Source/Model2/M2Shared.h"
#include "WoW/Source/Extension/ExtensionMgr.h"

namespace {

// Logs the first few model files as their .m2 and .skin arrive, with what the native objects
// expose at that point, and keeps instance counts so the create/load/destroy balance can be read
// in the log.
class HelloWorldModelScript : public ModelScript {
    public:
    HelloWorldModelScript() : ModelScript("HelloWorldModelScript") {}

    void OnModelSharedLoaded(CM2Shared& shared) override {
        m_sharedLoaded++;

        if (m_sharedLoaded <= 3) {
            sExtensionMgr->Log("HelloWorld: OnModelSharedLoaded %s (%u bytes, %u bone(s), %u sequence(s), %u texture(s))", shared.m_fileName, shared.m_fileSize, shared.m_data->m_bones.m_count, shared.m_data->m_sequences.m_count, shared.m_data->m_textures.m_count);
        }
    }

    void OnModelSkinLoaded(CM2Shared& shared) override {
        m_skinsLoaded++;

        if (m_skinsLoaded <= 3) {
            sExtensionMgr->Log("HelloWorld: OnModelSkinLoaded %s (%u section(s), %u batch(es))", shared.m_fileName, shared.m_skinData->m_skinSections.m_count, shared.m_skinData->m_batches.m_count);
        }
    }

    void OnModelSharedDestroy(CM2Shared& shared) override {
        m_sharedDestroyed++;
    }

    void OnModelCreated(CM2Model& model) override {
        m_created++;
    }

    void OnModelLoaded(CM2Model& model) override {
        m_loaded++;

        if (m_loaded == 1) {
            sExtensionMgr->Log("HelloWorld: OnModelLoaded (first instance)");
        }
    }

    void OnModelDestroy(CM2Model& model) override {
        m_destroyed++;

        if (m_destroyed % 500 == 0) {
            sExtensionMgr->Log("HelloWorld: models %u created, %u loaded, %u destroyed; files %u loaded, %u skins, %u freed", m_created, m_loaded, m_destroyed, m_sharedLoaded, m_skinsLoaded, m_sharedDestroyed);
        }
    }

    private:
    uint32_t m_sharedLoaded = 0;
    uint32_t m_skinsLoaded = 0;
    uint32_t m_sharedDestroyed = 0;
    uint32_t m_created = 0;
    uint32_t m_loaded = 0;
    uint32_t m_destroyed = 0;
};

}

void AddHelloWorldModelScripts() {
    new HelloWorldModelScript();
}
