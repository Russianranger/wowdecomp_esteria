// Compile without linking, with the same capacity definitions as the client.
// Example: clang++ -std=c++17 -I. -fsyntax-only Tools/check_extended_layout.cpp
#include <cstddef>
#include <type_traits>
#include "WoW/Source/Object/ExtendedLimits.h"
#include "WoW/Source/Object/Player.h"
#include "WoW/Source/Object/Container.h"
#include "WoW/Source/ObjectMgrClient/Mirror.h"

static_assert(sizeof(PlayerQuestLogEntry) == 20, "A quest occupies five update dwords");
static_assert(sizeof(CGPlayerData) == 4 * ExtendedLimits::PlayerMirrorDwords, "Remote player layout");
static_assert(sizeof(CGPlayerLocalData) == 4 * ExtendedLimits::PlayerDataDwords, "Local player layout");
static_assert(sizeof(CGContainerData) == 4 * ExtendedLimits::ContainerDataDwords, "Container layout");
static_assert(std::extent<decltype(CGPlayerData::m_playerQuestLog)>::value == ExtendedLimits::QuestSlots, "Quest storage capacity");
static_assert(std::extent<decltype(CGContainerData::m_containerFieldSlot1)>::value == ExtendedLimits::ContainerSlots, "Container storage capacity");
static_assert(offsetof(CGContainerData, m_containerFieldSlot1) == 8, "Container slot base");
static_assert(std::extent<decltype(CGPlayer::s_mirrorFields)>::value == ExtendedLimits::PlayerSharedSavedFields, "Shared mirror map capacity");
static_assert(std::extent<decltype(CGPlayer::s_localMirrorFields)>::value == 870, "Local mirror map capacity");
static_assert(std::extent<decltype(CGContainer::s_mirrorFields)>::value == 2 * ExtendedLimits::ContainerSlots, "Container mirror map capacity");
static_assert(std::extent<decltype(PLAYER_DESCRIPTORS)>::value == ExtendedLimits::PlayerDescriptorCount, "Player descriptor count");
static_assert(std::extent<decltype(s_playerDescriptors)>::value == ExtendedLimits::PlayerDwords, "Expanded player descriptor capacity");
static_assert(std::extent<decltype(s_containerDescriptors)>::value == ExtendedLimits::ContainerDwords, "Expanded container descriptor capacity");
static_assert(ExtendedLimits::PlayerDwords <= 32 * WOWDECOMP_MAX_UPDATE_MASKS, "Player mask coverage");

// Every field following the quest array must move by the same byte delta.
#define CHECK_PLAYER_OFFSET(type, field, stock) \
    static_assert(offsetof(type, field) == ExtendedLimits::PlayerOffset(stock), #field " layout")
CHECK_PLAYER_OFFSET(CGPlayerData, m_playerQuestLog, 0x0028);
CHECK_PLAYER_OFFSET(CGPlayerData, m_playerVisibleItems, 0x021C);
CHECK_PLAYER_OFFSET(CGPlayerData, m_playerChosenTitle, 0x02B4);
CHECK_PLAYER_OFFSET(CGPlayerData, m_playerFakeInebriation, 0x02B8);
CHECK_PLAYER_OFFSET(CGPlayerData, m_playerFieldPad0, 0x02BC);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldInvSlotHead, 0x02C0);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldPackSlot1, 0x0378);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldBankSlot1, 0x03F8);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldBankbagSlot1, 0x04D8);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldVendorbuybackSlot1, 0x0510);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldKeyringSlot1, 0x0570);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldCurrencytokenSlot1, 0x0670);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFarsight, 0x0770);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldKnownTitles, 0x0778);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldKnownCurrencies, 0x0790);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerXp, 0x0798);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerNextLevelXp, 0x079C);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerSkillInfo, 0x07A0);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerCharacterPoints, 0x0DA0);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerTrackCreatures, 0x0DA8);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerTrackResources, 0x0DAC);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerBlockPercentage, 0x0DB0);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerDodgePercentage, 0x0DB4);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerParryPercentage, 0x0DB8);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerExpertise, 0x0DBC);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerOffhandExpertise, 0x0DC0);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerCritPercentage, 0x0DC4);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerRangedCritPercentage, 0x0DC8);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerOffhandCritPercentage, 0x0DCC);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerSpellCritPercentage1, 0x0DD0);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerShieldBlock, 0x0DEC);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerShieldBlockCritPercentage, 0x0DF0);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerExploredZones1, 0x0DF4);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerRestStateExperience, 0x0FF4);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldCoinage, 0x0FF8);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldModDamageDonePos, 0x0FFC);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldModDamageDoneNeg, 0x1018);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldModDamageDonePct, 0x1034);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldModHealingDonePos, 0x1050);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldModHealingPct, 0x1054);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldModHealingDonePct, 0x1058);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldModTargetResistance, 0x105C);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldModTargetPhysicalResistance, 0x1060);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldBytes, 0x1064);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerAmmoId, 0x1068);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerSelfResSpell, 0x106C);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldPvpMedals, 0x1070);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldBuybackPrice1, 0x1074);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldBuybackTimestamp1, 0x10A4);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldKills, 0x10D4);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldTodayContribution, 0x10D8);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldYesterdayContribution, 0x10DC);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldLifetimeHonorbaleKills, 0x10E0);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldBytes2, 0x10E4);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldWatchedFactionIndex, 0x10E8);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldCombatRating1, 0x10EC);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldArenaTeamInfo11, 0x1150);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldHonorCurrency, 0x11A4);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldArenaCurrency, 0x11A8);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldMaxLevel, 0x11AC);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldDailyQuests1, 0x11B0);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerRuneRegen1, 0x1214);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerNoReagentCost1, 0x1224);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldGlyphSlots1, 0x1230);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerFieldGlyphs1, 0x1248);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerGlyphsEnabled, 0x1260);
CHECK_PLAYER_OFFSET(CGPlayerLocalData, m_playerPetSpellPower, 0x1264);
#undef CHECK_PLAYER_OFFSET
