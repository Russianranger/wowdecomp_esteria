#ifndef WOW_SOURCE_OBJECT_EXTENDEDLIMITS_H
#define WOW_SOURCE_OBJECT_EXTENDEDLIMITS_H

#include <cstdint>

// CMake supplies these only for opted-in layouts. Standalone/default builds stay 12340.
#ifndef WOWDECOMP_QUEST_LOG_SLOTS
#define WOWDECOMP_QUEST_LOG_SLOTS 25
#endif
#ifndef WOWDECOMP_CONTAINER_SLOTS
#define WOWDECOMP_CONTAINER_SLOTS 36
#endif
#ifndef WOWDECOMP_MAX_UPDATE_MASKS
#define WOWDECOMP_MAX_UPDATE_MASKS 42
#endif

namespace ExtendedLimits {
constexpr uint32_t QuestSlots = WOWDECOMP_QUEST_LOG_SLOTS;
constexpr uint32_t ContainerSlots = WOWDECOMP_CONTAINER_SLOTS;
constexpr uint32_t QuestExtraDwords = 5 * (QuestSlots - 25);
constexpr uint32_t PlayerDwords = 1326 + QuestExtraDwords;
constexpr uint32_t RemotePlayerDwords = 324 + QuestExtraDwords;
constexpr uint32_t PlayerDataDwords = 1178 + QuestExtraDwords;
constexpr uint32_t PlayerMirrorDwords = 176 + QuestExtraDwords;
constexpr uint32_t PlayerSavedFields = 1043 + QuestExtraDwords;
constexpr uint32_t PlayerSharedSavedFields = 173 + QuestExtraDwords;
constexpr uint32_t PlayerDescriptorCount = 215 + 4 * (QuestSlots - 25);
constexpr uint32_t ContainerDwords = 66 + 2 * ContainerSlots;
constexpr uint32_t ContainerDataDwords = 2 + 2 * ContainerSlots;
constexpr uint32_t MaxManagedDwords = PlayerDwords;

// Translate original byte offsets used by the reconstructed mirror handlers.
constexpr uint32_t PlayerOffset(uint32_t stockOffset) {
    return stockOffset < 0x21C ? stockOffset : stockOffset + 4 * QuestExtraDwords;
}

static_assert(QuestSlots >= 25 && QuestSlots <= 1000, "Supported quest capacity: 25..1000");
static_assert(ContainerSlots >= 36 && ContainerSlots <= 254, "Supported container capacity: 36..254");
static_assert(WOWDECOMP_MAX_UPDATE_MASKS >= 42 && WOWDECOMP_MAX_UPDATE_MASKS <= 255, "Update-mask count must fit a byte");
static_assert(PlayerDwords <= 32 * WOWDECOMP_MAX_UPDATE_MASKS, "Player fields exceed update masks");
static_assert(ContainerDwords <= 32 * WOWDECOMP_MAX_UPDATE_MASKS, "Container fields exceed update masks");
}

#endif
