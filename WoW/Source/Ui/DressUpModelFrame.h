#ifndef WOW_SOURCE_UI_DRESSUPMODELFRAME_H
#define WOW_SOURCE_UI_DRESSUPMODELFRAME_H

#include <cstddef>
#include <cstdint>
#include "Engine/Source/Base/CDataAllocator.h"
#include "WoW/Source/Ui/CharacterModelBase.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"

class CCharacterComponent;
class CSimpleFrame;
class CGItem_C;
class CM2Model;
class CRenderBatch;
struct lua_State;

class CGDressUpModelFrame : public CGCharacterModelBase {
    public:
    static CDataAllocator s_dressUpModelHeap;
    static int32_t s_metatable;
    static int32_t s_objectType;
    static const char* s_objectTypeName;

    static void operator delete(void* ptr);
    static CSimpleFrame* Create(CSimpleFrame* parent);
    static void CreateScriptMetaTable();
    static void DestroyScriptMetaTable();
    static int32_t GetObjectType();
    static void RegisterScriptMethods(lua_State* L);

    /* 0380 */ CCharacterComponent* m_component;
    /* 0384 */ CGUnitVirtualItem m_mainHand;
    /* 038C */ CGUnitVirtualItem m_offHand;
    /* 0394 */ uint8_t m_isNativeDisplay;
    /* 0395 */ uint8_t m_sheathed; // NOT IN BINARY: TryOn puts hand items on the sheathe point instead of in the hand
    /* 0396 */ uint8_t m_bodyHidden; // NOT IN BINARY: nothing of the character itself draws, only what it holds
    /* 0397 */ uint8_t m_silhouette; // NOT IN BINARY: the character draws as a dark silhouette
    uint8_t m_previewSex; // NOT IN BINARY: 0 the unit's own look, 1 a human man, 2 a human woman
    uint8_t m_previewHair; // NOT IN BINARY: hair style of the human preview
    uint8_t m_snapshotOnly; // NOT IN BINARY: never draws on screen, only into snapshots
    uint8_t m_snapshotPending; // NOT IN BINARY: m_snapshotKey is waiting for the model to be ready
    uint8_t m_ready; // NOT IN BINARY: last UpdateModel found the character fully built
    uint8_t m_readyFrames; // NOT IN BINARY: frames in a row m_ready has held
    uint16_t m_drawnFrames; // NOT IN BINARY: frames the pending snapshot has been drawn for
    uint8_t m_needAttach; // NOT IN BINARY: the pending snapshot is of an item that hangs on the model (helm, shoulders, weapon)
    uint8_t m_force; // NOT IN BINARY: the pending snapshot is stored even when it looks empty (the last tries of a job)
    uint8_t m_commitNext; // NOT IN BINARY: drawn for the last time, read back at the start of the next frame (the draw is only finished by then)
    uint16_t m_minFrames; // NOT IN BINARY: at least this many, so effect models and particles have time to appear
    char m_snapshotKey[48];
    char m_refKey[48]; // NOT IN BINARY: the stored snapshot of the bare character the pending one must differ from, or empty

    ~CGDressUpModelFrame() override;
    int32_t GetScriptMetaTable() override;
    bool IsA(int32_t type) override;
    bool IsA(const char* typeName) override;
    const char* GetObjectTypeName() override;
    void UpdateModel() override;
    void OnFrameRender(CRenderBatch* batch, uint32_t layer) override;
    void SetModel(CM2Model* model) override;
    CM2Model* GetDuplicateUnitCharacterModel(CGUnit_C* unit) override;

    static void RenderSnapshot(void* arg);

    CGDressUpModelFrame(CSimpleFrame* parent);
    void Undress();
    void TryOn(int32_t itemId, const uint32_t* enchants, int32_t hand);
    void TryOnWithVisual(int32_t itemId, int32_t visual, int32_t hand);
    void TryOnItem(CGItem_C* item, int32_t hand);
};

#endif
