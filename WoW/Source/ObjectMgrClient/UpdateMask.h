#ifndef WOW_SOURCE_OBJECTMGRCLIENT_UPDATEMASK_H
#define WOW_SOURCE_OBJECTMGRCLIENT_UPDATEMASK_H

#include <cstdint>

// Check every advertised bit before copying fields or firing handlers. In
// particular, a set padding bit must not leave an unread value in the packet.
inline bool ValidateUpdateMask(const uint32_t* masks, uint32_t blockCount,
                               uint32_t fieldCount, uint32_t availableDwords) {
    uint32_t values = 0;
    for (uint32_t block = 0; block < blockCount; ++block) {
        uint32_t mask = masks[block];
        const uint32_t base = block * 32;
        if (base >= fieldCount) {
            if (mask) {
                return false;
            }
        } else if (fieldCount - base < 32 && (mask >> (fieldCount - base))) {
            return false;
        }
        while (mask) {
            mask &= mask - 1;
            ++values;
        }
    }
    return values <= availableDwords;
}

#endif
