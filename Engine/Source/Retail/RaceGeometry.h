#ifndef ENGINE_SOURCE_RETAIL_RACEGEOMETRY_H
#define ENGINE_SOURCE_RETAIL_RACEGEOMETRY_H

#include <cstdint>
#include <vector>
#include "Engine/Source/Retail/SourceContext.h"

namespace Retail {

// These anatomy groups are hidden by the retail visibility reset. A recipe only names groups
// controlled by its choices: Dwarves omit the head, while Night/Blood/Void Elves omit their
// separate hand mesh (23). Glove cuffs use group 4 and do not supply those fingers/palms.
// Explicit choices (including a hidden group value) always take precedence over these defaults.
inline std::vector<uint16_t> DefaultRaceGeosets(const std::vector<uint16_t>& recipe) {
    std::vector<uint16_t> defaults;
    for (const uint16_t base : {2001, 2201, 2301, 3202, 3301, 5101}) {
        bool chosen = false;
        for (const uint16_t geoset : recipe) chosen = chosen || geoset / 100 == base / 100;
        if (!chosen) defaults.push_back(base);
    }
    return defaults;
}

// Forever's playable Skyborne eye colors select texture type 19 on geoset 3301.
// The inherited donor's 17xx meshes use lightning atlases; no eligible Skyborne
// choice selects them. Group 51 is an authored eyesight/blindness overlay and stays.
// Apply after Wrath's selector/DK pass, which would otherwise restore 1701/1703.
template <class Model>
void ApplyRaceEyeGeometry(Model& model, SourceKind source, uint32_t sourceRace) {
    if (source != SourceKind::Forever || (sourceRace != 95 && sourceRace != 96)) return;
    model.SetGeometryVisible(1701, 1799, 0);
}

// Legacy boot styles select the shin (group 5), not Retail foot shapes (group 20). Even style
// zero boots need the closed shoe mesh. A robe changes the shins but does not unequip the boots.
template <class Model>
void ApplyLegacyFootGeometry(Model& model, uint32_t bootsDisplayId, bool hasClosedFoot) {
    const uint16_t foot = bootsDisplayId && hasClosedFoot ? 2002 : 2001;
    model.SetGeometryVisible(2001, 2099, 0);
    model.SetGeometryVisible(foot, foot, 1);
}

}
#endif
