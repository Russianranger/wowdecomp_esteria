#ifndef WOW_COMMON_COMPONENTCORE_LZNT1_H
#define WOW_COMMON_COMPONENTCORE_LZNT1_H

#include <cstddef>
#include <cstdint>

namespace Esteria {

// Decode a complete LZNT1 stream into caller-owned storage without allocating.
// Source and destination must not overlap. On failure, output may be partially
// written but written is reset to zero; callers must discard that output.
// The caller must additionally check written against the expected asset size.
bool DecodeLznt1(const uint8_t* source, size_t sourceSize,
                 uint8_t* destination, size_t destinationSize,
                 size_t& written);

}

#endif
