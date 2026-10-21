#include <cstdio>
#include <cstring>

#include "Engine/Source/Base/CDataStore.h"
#include "Engine/Source/Retail/BodyShape.h"
#include "Engine/Source/Retail/RetailLookWire.h"
#include "WoW/Common/ComponentCore/RetailRace.h"
#include "WoW/Source/Extension/ExtensionMgr.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"

// Body sliders of the players in view. The server sends one SMSG_ESTERIA_BODY_SHAPE per player when that player
// comes into view (and for the local player at login): guid (8 bytes) + the BodyShapeWire payload. The values are
// kept by guid and applied to each player's model from CGUnit_C::PreAnimate.

namespace {
constexpr uint32_t SMSG_ESTERIA_BODY_SHAPE = 0x523;
constexpr uint32_t SMSG_ESTERIA_RETAIL_LOOK = 0x524;

int32_t Receive(void*, NETMESSAGE, uint32_t, CDataStore* message) {
    constexpr uint32_t Size = 8 + CharacterBodyShape::PayloadSize;
    if (!message || message->m_read > message->m_size || message->m_size - message->m_read != Size
        || !message->FetchRead(message->m_read, Size)) {
        return 0;
    }

    uint8_t bytes[Size];
    message->GetData(bytes, Size);
    uint64_t guid;
    std::memcpy(&guid, bytes, sizeof(guid));

    CharacterBodyShape::Values wire{};
    if (!CharacterBodyShape::ReadPayload(bytes + 8, CharacterBodyShape::PayloadSize, wire)) {
        return 0;
    }

    BodyShape::Values values = BodyShape::FromWire(wire);
    BodyShape::SetUnit(guid, values);

    // The local player's own sliders start from what the server saved.
    if (guid == ClntObjMgrGetActivePlayer().m_guid) {
        BodyShape::Local() = values;
    }
    return 1;
}

// guid (8 bytes) + the RetailLookWire payload: the full Retail customization of a player in view.
int32_t ReceiveLook(void*, NETMESSAGE, uint32_t, CDataStore* message) {
    constexpr uint32_t Size = 8 + CharacterRetailLook::PayloadSize;
    if (!message || message->m_read > message->m_size || message->m_size - message->m_read != Size
        || !message->FetchRead(message->m_read, Size)) {
        return 0;
    }

    uint8_t bytes[Size];
    message->GetData(bytes, Size);
    uint64_t guid;
    std::memcpy(&guid, bytes, sizeof(guid));

    CharacterRetailLook::Look look;
    if (!CharacterRetailLook::ReadPayload(bytes + 8, CharacterRetailLook::PayloadSize, look)) {
        return 0;
    }

    char line[120];
    std::snprintf(line, sizeof(line), "look received: guid=%llx race=%u sex=%u options=%u", static_cast<unsigned long long>(guid),
        unsigned(look.race), unsigned(look.sex), unsigned(look.count));
    BodyShape::Log(line);
    RetailRaceSetLook(guid, look);
    return 1;
}

class Lifecycle : public ClientScript {
public:
    Lifecycle() : ClientScript("BodyShapeSyncLifecycle") { }
    void OnLeaveWorld() override {
        BodyShape::Units().clear();
        RetailRaceClearLooks();
        BodyShape::Local() = BodyShape::Values();
    }
};
}

void AddBodyShapeSyncExtension() {
    sExtensionMgr->RegisterMessageHandler(SMSG_ESTERIA_BODY_SHAPE, Receive, nullptr);
    sExtensionMgr->RegisterMessageHandler(SMSG_ESTERIA_RETAIL_LOOK, ReceiveLook, nullptr);
    new Lifecycle();
}
