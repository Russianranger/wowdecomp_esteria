#ifndef WOW_SOURCE_UI_LOOTBEAM_H
#define WOW_SOURCE_UI_LOOTBEAM_H

#include <cstddef>
#include <cstdint>

// Not in the original 3.3.5a binary. A beam of light that rises from every NPC corpse that can still be
// looted, ported from the WXL module wxl-loot-beam (WarcraftXL), where it was a client extension drawn
// from the core's render events.
//
// It is drawn additively inside the world render, depth-tested against the scene so terrain and walls hide it.
//
// The colour is the quality of the rarest item in the corpse's loot. The stock client only learns a
// corpse's loot when loot is requested for it, so a body colours itself when the loot window opens; the
// companion AzerothCore module mod-loot-beam additionally writes the quality onto the corpse's own
// UNIT_FIELD_PADDING when it dies, and that hint is merged in so the beam is right the moment the body
// falls. See UnitLootBeamField.
//
// Options: the lootBeam, lootBeamHeight and lootBeamSparkles cvars, shown in the Esteria video options.
namespace LootBeam
{
    // Registers the lootBeam, lootBeamHeight and lootBeamSparkles cvars. Called once at client start.
    void RegisterCVars();

    // Called once per frame from the world render, after the scene's geometry and before the interface.
    // Scans the resident units for lootable corpses, advances their beacons and draws them.
    void Update(float elapsedSec);
}

#endif
