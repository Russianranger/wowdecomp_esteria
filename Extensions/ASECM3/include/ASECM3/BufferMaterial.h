#pragma once
#include "Document.h"

namespace ASECM3 {
// Indexed MADD parameter storage observed in the supplied HotS cowl files.
// IDs, byte widths and flags are retained, not interpreted as shader roles or
// opcodes. In particular an eight-byte field is not automatically a texture.
struct BufferParameter {
    uint32_t id = 0;
    uint16_t byteWidth = 0;
    uint8_t flags = 0;
    uint64_t sourceOffset = 0;
    std::vector<uint8_t> bytes;
};
struct BufferParameterGroup {
    uint32_t id = 0;
    uint64_t sourceOffset = 0;
    std::vector<BufferParameter> parameters;
};
struct BufferMaterial {
    std::vector<BufferParameterGroup> groups;
};
BufferMaterial DecodeBufferMaterial(const std::vector<uint8_t>& bytes,
                                   const std::vector<uint32_t>& expectedGroupIds);
}
