#ifndef WOW_SOURCE_UI_DIGITALINVENTORYCOUNTS_H
#define WOW_SOURCE_UI_DIGITALINVENTORYCOUNTS_H

#include <cstdint>
#include <unordered_map>

// Page-independent authoritative virtual carried counts. Cleared on leaving the character.
namespace EsteriaInventoryClient {
struct CountEntry {
    uint32_t count = 0;
    uint32_t totemCategory = 0;
};
inline std::unordered_map<uint32_t, CountEntry> counts;
inline uint32_t Count(uint32_t entry) {
    auto itr = counts.find(entry);
    return itr == counts.end() ? 0 : itr->second.count;
}
}

#endif
