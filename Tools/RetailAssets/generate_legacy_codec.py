"""Freeze installed packed appearance choice IDs for the native retail renderer.

Input is the audited saved-codecs.json (exported from the installed race codec manifests).
Never regenerate choice order from a newer Retail DB2 catalog: saved indexes name the old IDs.
"""
import argparse
import json
from pathlib import Path

def generate(profiles):
    choices, options = [], []
    for profile in profiles:
        for target in profile['targets']:
            for option in profile['options']:
                field, count, factor = option['descriptor']
                assert count == len(option['choices']) and 0 <= field < 13 and count*factor <= 256
                assert not option['missingCurrent']
                offset = len(choices)
                choices.extend(option['choices'])
                options.append((target, profile['sex'], option['id'], field, count, factor, offset))
    lines = ['// Generated from audited installed codec manifests; indexes retain frozen source choice IDs.',
             '#ifndef ENGINE_SOURCE_RETAIL_LEGACYAPPEARANCE_H', '#define ENGINE_SOURCE_RETAIL_LEGACYAPPEARANCE_H',
             '#include <array>', '#include <cstdint>', 'namespace Retail::LegacyAppearance {',
             'struct Option { uint32_t race, sex, id, field, count, factor, offset; };',
             'inline constexpr uint32_t Choices[] = {']
    for i in range(0,len(choices),16): lines.append('    '+', '.join(map(str,choices[i:i+16]))+',')
    lines += ['};','inline constexpr Option Options[] = {']
    lines += ['    {'+', '.join(map(str,o))+'},' for o in options]
    lines += ['};', '''inline bool IsPackedRace(uint32_t race) {
    for (const auto& option : Options) if (option.race == race) return true;
    return false;
}
inline const Option* Find(uint32_t race, uint32_t sex, uint32_t id) {
    for (const auto& option : Options)
        if (option.race == race && option.sex == sex && option.id == id) return &option;
    return nullptr;
}
inline bool Decode(uint32_t race, uint32_t sex, uint32_t id, const std::array<uint8_t, 13>& fields, uint32_t& choice) {
    const Option* option = Find(race, sex, id);
    if (!option) return false;
    choice = Choices[option->offset + (fields[option->field] / option->factor) % option->count];
    return true;
}
inline bool ValidateFields(uint32_t race, uint32_t sex, const std::array<uint8_t, 13>& fields) {
    std::array<uint32_t, 13> capacities{};
    bool found = false;
    for (const auto& option : Options) if (option.race == race && option.sex == sex) {
        found = true;
        capacities[option.field] += (option.count - 1) * option.factor;
    }
    for (uint32_t i = 0; i < 13; ++i) if (fields[i] > capacities[i]) return false;
    return found;
}
inline bool Encode(uint32_t race, uint32_t sex, uint32_t id, uint32_t choice, std::array<uint8_t, 13>& fields) {
    const Option* option = Find(race, sex, id);
    if (!option) return false;
    for (uint32_t i = 0; i < option->count; ++i) if (Choices[option->offset + i] == choice) {
        const uint32_t value = fields[option->field];
        const uint32_t updated = value - (value / option->factor % option->count) * option->factor + i * option->factor;
        if (updated > 255) return false;
        fields[option->field] = uint8_t(updated);
        return true;
    }
    return false; // New Retail choices cannot silently change an installed codec's radix.
}
} // namespace Retail::LegacyAppearance
#endif''']
    return '\n'.join(lines)+'\n'

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input',type=Path)
    parser.add_argument('output',type=Path)
    args=parser.parse_args()
    args.output.write_text(generate(json.loads(args.input.read_text())),encoding='utf-8')
