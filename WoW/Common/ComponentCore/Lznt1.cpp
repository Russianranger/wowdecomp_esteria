#include "Lznt1.h"

#include <cstring>

namespace Esteria {

// LZNT1 framing and token layout: Microsoft [MS-XCA] sections 2.5.1-2.5.3.
// https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-xca/5655f4a3-6ba4-489b-959f-e1f407c52f15
bool DecodeLznt1(const uint8_t* source, size_t sourceSize,
                 uint8_t* destination, size_t destinationSize,
                 size_t& written) {
    written = 0;
    if ((!source && sourceSize) || (!destination && destinationSize)) {
        return false;
    }

    size_t input = 0;
    size_t output = 0;
    while (input < sourceSize) {
        if (sourceSize - input < 2) {
            return false;
        }
        const uint16_t header = static_cast<uint16_t>(source[input])
            | (static_cast<uint16_t>(source[input + 1]) << 8);
        input += 2;
        if (header == 0) {
            // The optional end marker is only valid at the end of this asset.
            if (input != sourceSize) {
                return false;
            }
            written = output;
            return true;
        }
        if ((header & 0x7000) != 0x3000) {
            return false;
        }
        const size_t packedSize = (header & 0x0fff) + 1;
        if (packedSize > sourceSize - input) {
            return false;
        }
        const size_t chunkEnd = input + packedSize;
        const size_t chunkStart = output;

        if ((header & 0x8000) == 0) {
            if (packedSize > destinationSize - output) {
                return false;
            }
            std::memcpy(destination + output, source + input, packedSize);
            output += packedSize;
            input = chunkEnd;
        } else {
            while (input < chunkEnd) {
                const uint8_t flags = source[input++];
                // A flag group must contain at least one literal or token.
                if (input == chunkEnd) {
                    return false;
                }
                for (unsigned bit = 0; bit < 8 && input < chunkEnd; ++bit) {
                    const size_t chunkOffset = output - chunkStart;
                    if ((flags & (1u << bit)) == 0) {
                        if (chunkOffset >= 4096 || output == destinationSize) {
                            return false;
                        }
                        destination[output++] = source[input++];
                    } else {
                        if (chunkEnd - input < 2 || chunkOffset == 0) {
                            return false;
                        }
                        const uint16_t token = static_cast<uint16_t>(source[input])
                            | (static_cast<uint16_t>(source[input + 1]) << 8);
                        input += 2;
                        unsigned displacementShift = 12;
                        uint16_t lengthMask = 0x0fff;
                        // Displacement grows at offsets 17, 33, 65, ... within
                        // each independently compressed 4096-byte chunk.
                        for (size_t position = chunkOffset - 1; position >= 16; position >>= 1) {
                            --displacementShift;
                            lengthMask >>= 1;
                        }
                        const size_t displacement = (token >> displacementShift) + 1;
                        const size_t length = (token & lengthMask) + 3;
                        if (displacement > chunkOffset || length > 4096 - chunkOffset
                            || length > destinationSize - output) {
                            return false;
                        }
                        // Forward byte copy is required: an overlapping match
                        // can refer to bytes produced earlier in this match.
                        for (size_t i = 0; i < length; ++i) {
                            destination[output] = destination[output - displacement];
                            ++output;
                        }
                    }
                }
            }
        }
        // Every chunk except the final chunk represents 4096 input bytes.
        // Reject short intermediate chunks instead of silently synthesizing
        // pixels or accepting a cross-chunk history reference.
        if (output - chunkStart != 4096 && input < sourceSize
            && !(sourceSize - input == 2 && source[input] == 0 && source[input + 1] == 0)) {
            return false;
        }
    }
    written = output;
    return true;
}

}
