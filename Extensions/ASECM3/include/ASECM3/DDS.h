#pragma once
#include "Document.h"
namespace ASECM3 {
enum class DDSFormat { BC1,BC2,BC3,BC4,BC5,BC7,RGBA8,BGRA8 };
struct DDSMip {
    uint32_t face=0,level=0,width=0,height=0;
    size_t offset=0,size=0,rowBytes=0;
};
class DDSImage {
    std::vector<uint8_t> m_bytes;
public:
    uint32_t width=0,height=0,faces=1,levels=1;
    DDSFormat format=DDSFormat::BC1;
    bool srgb=false,premultiplied=false;
    std::vector<DDSMip> mips;
    static DDSImage Parse(std::vector<uint8_t> bytes);
    const uint8_t* Pixels(const DDSMip& mip) const;
};
const char* DDSFormatName(DDSFormat format);
}
