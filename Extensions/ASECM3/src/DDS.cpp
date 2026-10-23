#include "ASECM3/DDS.h"
#include <algorithm>
#include <cstring>
namespace ASECM3 {
const char* DDSFormatName(DDSFormat format) {
    switch(format) {
        case DDSFormat::BC1:return "BC1";case DDSFormat::BC2:return "BC2";case DDSFormat::BC3:return "BC3";
        case DDSFormat::BC4:return "BC4";case DDSFormat::BC5:return "BC5";case DDSFormat::BC7:return "BC7";
        case DDSFormat::RGBA8:return "RGBA8";case DDSFormat::BGRA8:return "BGRA8";
    }
    return "Unknown";
}
const uint8_t* DDSImage::Pixels(const DDSMip& mip) const {
    if (mip.offset>m_bytes.size() || mip.size>m_bytes.size()-mip.offset) throw Error("DDS mip exceeds owned texture data");
    return m_bytes.data()+mip.offset;
}
DDSImage DDSImage::Parse(std::vector<uint8_t> bytes) {
    if (bytes.size()<128 || bytes.size()>512ull*1024*1024) throw Error("DDS input size is invalid");
    auto read=[&](size_t p) {if (p>bytes.size() || bytes.size()-p<4) throw Error("Truncated DDS header"); return uint32_t(bytes[p])|uint32_t(bytes[p+1])<<8|uint32_t(bytes[p+2])<<16|uint32_t(bytes[p+3])<<24;};
    if (read(0)!=0x20534444 || read(4)!=124 || read(76)!=32) throw Error("DDS signature/header size mismatch");
    DDSImage out;out.width=read(16);out.height=read(12);out.levels=std::max(1u,read(28));
    if (!out.width || !out.height || out.width>16384 || out.height>16384) throw Error("DDS dimensions exceed texture limits");
    uint32_t maximumLevels=1;
    for (uint32_t dimension=std::max(out.width,out.height);dimension>1;dimension/=2) ++maximumLevels;
    if (out.levels>maximumLevels) throw Error("DDS mip count exceeds its dimensions");
    if (read(24)>1 || (read(112)&0x200000)) throw Error("DDS volume textures are not supported by this 2D/cube reader");
    const bool legacyCube=(read(112)&0x200)!=0;
    if (legacyCube && (read(112)&0xfc00)!=0xfc00) throw Error("DDS cubemap is missing one or more faces");
    out.faces=legacyCube?6:1;size_t offset=128;uint32_t block=0;
    const auto code=read(84),flags=read(80);
    if (flags&4) {
        switch(code) {
            case 0x31545844:out.format=DDSFormat::BC1;block=8;break; // DXT1
            case 0x32545844:out.premultiplied=true;[[fallthrough]];
            case 0x33545844:out.format=DDSFormat::BC2;block=16;break;
            case 0x34545844:out.premultiplied=true;[[fallthrough]];
            case 0x35545844:out.format=DDSFormat::BC3;block=16;break;
            case 0x31495441:case 0x55344342:out.format=DDSFormat::BC4;block=8;break;
            case 0x32495441:case 0x55354342:out.format=DDSFormat::BC5;block=16;break;
            case 0x30315844: {
                if(bytes.size()<148)throw Error("DDS DX10 header is truncated");
                const auto dxgi=read(128),dimension=read(132),misc=read(136),array=read(140),alpha=read(144)&7;
                if(dimension!=3 || array!=1)throw Error("DDS DX10 arrays/non-2D resources require a different texture adapter");
                out.faces=(misc&4)?6:1;offset=148;out.premultiplied=alpha==2;
                switch(dxgi) {
                    case 71:case 72:out.format=DDSFormat::BC1;block=8;out.srgb=dxgi==72;break;
                    case 74:case 75:out.format=DDSFormat::BC2;block=16;out.srgb=dxgi==75;break;
                    case 77:case 78:out.format=DDSFormat::BC3;block=16;out.srgb=dxgi==78;break;
                    case 80:out.format=DDSFormat::BC4;block=8;break;
                    case 83:out.format=DDSFormat::BC5;block=16;break;
                    case 98:case 99:out.format=DDSFormat::BC7;block=16;out.srgb=dxgi==99;break;
                    case 28:case 29:out.format=DDSFormat::RGBA8;out.srgb=dxgi==29;break;
                    case 87:case 91:out.format=DDSFormat::BGRA8;out.srgb=dxgi==91;break;
                    default:throw Error("DDS DXGI texture format is not implemented");
                }
                break;
            }
            default:throw Error("DDS FourCC format is not implemented");
        }
    } else if ((flags&0x40) && read(88)==32 && read(96)==0x0000ff00 && read(104)==0xff000000) {
        if(read(92)==0x000000ff && read(100)==0x00ff0000)out.format=DDSFormat::RGBA8;
        else if(read(92)==0x00ff0000 && read(100)==0x000000ff)out.format=DDSFormat::BGRA8;
        else throw Error("DDS RGB channel masks are unsupported");
    } else throw Error("DDS uncompressed layout is unsupported");
    if (out.faces==6 && out.width!=out.height) throw Error("DDS cubemap faces must be square");
    if (!block && (read(8)&8) && read(20)!=out.width*4) throw Error("DDS padded uncompressed rows require a different upload layout");
    for(uint32_t face=0;face<out.faces;++face) {
        uint32_t width=out.width,height=out.height;
        for(uint32_t level=0;level<out.levels;++level) {
            const size_t row=block?size_t((width+3)/4)*block:size_t(width)*4;
            const size_t size=row*(block?(height+3)/4:height);
            if(offset>bytes.size() || size>bytes.size()-offset)throw Error("DDS mip payload is truncated");
            out.mips.push_back({face,level,width,height,offset,size,row});offset+=size;
            width=std::max(1u,width/2);height=std::max(1u,height/2);
        }
    }
    out.m_bytes=std::move(bytes);return out;
}
}
