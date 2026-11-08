#ifndef ENGINE_SOURCE_BLIZZARDCORE_SOURCE_PACKAGES_IOUNIT_READEXACTAT_H
#define ENGINE_SOURCE_BLIZZARDCORE_SOURCE_PACKAGES_IOUNIT_READEXACTAT_H

#include <cstdint>

namespace System_File {

// A successful positional read can stop before EOF. Advance only by the
// bytes actually returned, preserving the backend's result for a zero-byte request.
template <class Read>
bool ReadExactAt(void* buffer, int64_t offset, uint32_t count, Read read) {
    if (offset < 0 || (count && offset > INT64_MAX - (count - 1))) {
        return false;
    }

    uint8_t* destination = static_cast<uint8_t*>(buffer);
    for (;;) {
        int32_t requested = count > INT32_MAX ? INT32_MAX : static_cast<int32_t>(count);
        int32_t amount = requested;
        if (!read(destination, offset, &amount) || amount < 0 || amount > requested) {
            return false;
        }
        if (static_cast<uint32_t>(amount) == count) {
            return true;
        }
        if (!amount) {
            return false;
        }

        destination += amount;
        offset += amount;
        count -= static_cast<uint32_t>(amount);
    }
}

}

#endif
