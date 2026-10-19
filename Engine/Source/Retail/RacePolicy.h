#ifndef ENGINE_SOURCE_RETAIL_RACEPOLICY_H
#define ENGINE_SOURCE_RETAIL_RACEPOLICY_H
#include <cstdint>

namespace Retail {
// Helmet masks describe visual anatomy, not faction/skill eligibility. Always bound the shift:
// target custom races above 31 cannot index the installed 32-bit HelmetGeosetVisData directly.
inline uint32_t HelmetMaskRace(uint32_t retailRace) {
    switch (retailRace) {
        case 27: return 4;                  // Nightborne -> Night Elf
        case 28: return 6;                  // Highmountain -> Tauren
        case 29: return 10;                 // Void Elf -> Blood Elf
        case 30: return 11;                 // Lightforged -> Draenei
        case 31: return 8;                  // Zandalari -> Troll
        case 32: return 1;                  // Kul Tiran -> Human
        case 34: case 84: case 85: return 3; // Dark Iron / Earthen -> Dwarf
        case 35: return 9;                  // Vulpera -> Goblin
        case 36: return 2;                  // Mag'har -> Orc
        case 37: return 7;                  // Mechagnome -> Gnome
        case 86: case 91: return 4;         // Haranir -> Night Elf (both factions)
        default: return retailRace < 32 ? retailRace : 0;
    }
}

inline bool UsesLegacyClosedFeet(uint32_t retailRace) {
    switch (retailRace) {
        case 1: case 2: case 3: case 4: case 5: case 7: case 9: case 10:
        case 24: case 25: case 26: case 27: case 29: case 32: case 34: case 36:
            return true;
        default: return false; // Preserve hooves, exposed paws/toes and authored mechanical/Feet choices.
    }
}
}
#endif
