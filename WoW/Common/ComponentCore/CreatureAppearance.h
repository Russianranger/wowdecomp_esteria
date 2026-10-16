// Generated from the selected race sources by tools/creature_race_pack.py.
#ifndef ACORE_CREATURE_APPEARANCE_H
#define ACORE_CREATURE_APPEARANCE_H

#include <array>
#include <cstdint>

namespace CreatureAppearance
{
    struct Option
    {
        unsigned field;
        unsigned count;
        unsigned factor;
    };

    inline constexpr std::array<Option, 5> NagaMale = {{
        {0, 25, 1},
        {1, 1, 1},
        {2, 5, 1},
        {3, 15, 1},
        {4, 96, 1},
    }};

    inline constexpr std::array<Option, 5> NagaFemale = {{
        {0, 255, 1},
        {1, 1, 1},
        {2, 5, 1},
        {3, 80, 1},
        {4, 240, 1},
    }};

    inline constexpr std::array<Option, 5> TuskarrMale = {{
        {0, 7, 1},
        {1, 1, 1},
        {2, 7, 1},
        {3, 7, 1},
        {4, 7, 1},
    }};

    inline constexpr std::array<Option, 5> VrykulMale = {{
        {0, 6, 1},
        {1, 1, 1},
        {2, 6, 1},
        {3, 5, 1},
        {4, 6, 1},
    }};

    inline constexpr std::array<Option, 5> VrykulFemale = {{
        {0, 6, 1},
        {1, 1, 1},
        {2, 5, 1},
        {3, 5, 1},
        {4, 1, 1},
    }};

    inline constexpr std::array<Option, 5> ThinhumanMale = {{
        {0, 4, 1},
        {1, 1, 1},
        {2, 4, 1},
        {3, 4, 1},
        {4, 7, 1},
    }};

    inline constexpr std::array<Option, 5> SethrakMale = {{
        {0, 5, 1},
        {1, 1, 1},
        {2, 4, 1},
        {3, 4, 1},
        {4, 1, 1},
    }};

    inline constexpr std::array<Option, 5> SethrakFemale = {{
        {0, 1, 1},
        {1, 1, 1},
        {2, 1, 1},
        {3, 1, 1},
        {4, 1, 1},
    }};

    inline constexpr std::array<Option, 5> GnollMale = {{
        {0, 120, 1},
        {1, 54, 1},
        {2, 60, 1},
        {3, 48, 1},
        {4, 3, 1},
    }};

    inline constexpr std::array<Option, 5> SaberonMale = {{
        {0, 4, 1},
        {1, 10, 1},
        {2, 3, 1},
        {3, 3, 1},
        {4, 1, 1},
    }};

    inline constexpr std::array<Option, 5> Drakkin_AllianceMale = {{
        {0, 6, 1},
        {1, 1, 1},
        {2, 5, 1},
        {3, 7, 1},
        {4, 1, 1},
    }};

    inline constexpr std::array<Option, 5> Drakkin_AllianceFemale = {{
        {0, 16, 1},
        {1, 1, 1},
        {2, 1, 1},
        {3, 25, 1},
        {4, 1, 1},
    }};

    inline constexpr std::array<Option, 5> Drakkin_HordeMale = {{
        {0, 16, 1},
        {1, 1, 1},
        {2, 1, 1},
        {3, 25, 1},
        {4, 1, 1},
    }};

    inline constexpr std::array<Option, 5> Drakkin_HordeFemale = {{
        {0, 16, 1},
        {1, 1, 1},
        {2, 1, 1},
        {3, 25, 1},
        {4, 1, 1},
    }};

    inline constexpr std::array<Option, 5> Drakonid_AllianceMale = {{
        {0, 5, 1},
        {1, 4, 1},
        {2, 6, 1},
        {3, 76, 1},
        {4, 1, 1},
    }};

    inline constexpr std::array<Option, 5> Drakonid_HordeMale = {{
        {0, 5, 1},
        {1, 4, 1},
        {2, 6, 1},
        {3, 76, 1},
        {4, 1, 1},
    }};

    inline constexpr std::array<Option, 15> GnollControls = {{
        {0, 10, 1},
        {1, 6, 1},
        {1, 3, 6},
        {1, 3, 18},
        {0, 3, 10},
        {2, 3, 1},
        {2, 4, 3},
        {2, 5, 12},
        {3, 2, 1},
        {3, 2, 2},
        {0, 4, 30},
        {3, 2, 4},
        {3, 3, 8},
        {3, 2, 24},
        {4, 3, 1},
    }};

    inline constexpr std::array<char const*, 15> GnollLabels = {
        "Fur Color",
        "Mane",
        "Beard",
        "Ears",
        "Eyes",
        "Outfit Color",
        "Chest Sling",
        "Cuffs",
        "Waist",
        "Loincloth",
        "Tail",
        "Leg Wraps",
        "Pauldrons",
        "Belt Strap",
        "Trousers",
    };

    inline constexpr std::array<Option, 5> DrakonidControls = {{
        {0, 5, 1},
        {1, 2, 1},
        {2, 6, 1},
        {1, 2, 2},
        {3, 76, 1},
    }};

    inline constexpr std::array<char const*, 5> DrakonidLabels = {
        "Skin Color",
        "Head",
        "Horns",
        "Tail",
        "Eye Color",
    };

    constexpr bool Uses(unsigned race)
    {
        return (((((((race >= 54 && race <= 59) || race == 62) || race == 63) || race == 64) || race == 66) || race == 67) || race == 69) || race == 70;
    }

    constexpr bool GenderAllowed(unsigned race, unsigned gender)
    {
        return Uses(race) && (gender == 0
            || ((race == 54 || race == 56 || race == 57 || race == 62 || race == 66 || race == 67) && gender == 1));
    }

    constexpr bool UsesPlayerVrykul(unsigned race, unsigned gender)
    {
        return (race == 56 || race == 57) && gender == 1;
    }

    constexpr bool UsesStockMaterials(unsigned race, unsigned gender)
    {
        return UsesPlayerVrykul(race, gender) || (race == 66 && gender == 1) || (race == 67 && gender < 2);
    }

    constexpr std::array<Option, 5> const& Options(unsigned race, unsigned gender)
    {
        switch (race)
        {
            case 54:
                return gender ? NagaFemale : NagaMale;
            case 55:
                return TuskarrMale;
            case 56:
            case 57:
                return gender ? VrykulFemale : VrykulMale;
            case 58:
            case 59:
                return ThinhumanMale;
            case 62:
                return gender ? SethrakFemale : SethrakMale;
            case 63:
                return GnollMale;
            case 64:
                return SaberonMale;
            case 66:
                return gender ? Drakkin_AllianceFemale : Drakkin_AllianceMale;
            case 67:
                return gender ? Drakkin_HordeFemale : Drakkin_HordeMale;
            case 69:
                return Drakonid_AllianceMale;
            case 70:
                return Drakonid_HordeMale;
            default:
                return NagaMale;
        }
    }

    constexpr bool Validate(unsigned race, unsigned gender, std::array<std::uint8_t, 5> const& fields)
    {
        if (!GenderAllowed(race, gender))
            return false;
        auto const& options = Options(race, gender);
        for (unsigned i = 0; i < fields.size(); ++i)
            if (fields[i] >= options[i].count)
                return false;
        return true;
    }
}

#endif
