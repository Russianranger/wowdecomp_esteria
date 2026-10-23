#pragma once
#include "ASECM3/Document.h"
#include <cmath>
namespace ASECM3::Client {
struct WorldPlacement {unsigned passes=0;bool transparent=false,clip=false;};
// Native scene pass bits: 0 opaque, 1 above liquid, 2 below liquid.
inline WorldPlacement PlaceWorldPacket(bool deferred,float opacity,bool above,bool below,
                                      float distance,float radius,bool allowSplit,bool cameraBelow){
    if(!std::isfinite(opacity))throw Error("Nonfinite M3 world opacity");
    if(opacity<.0001f)return {};
    WorldPlacement out;out.transparent=deferred||opacity<.99999f;
    if(!out.transparent){out.passes=1;return out;}
    if(above&&below){
        if(!std::isfinite(distance)||!std::isfinite(radius)||radius<0)throw Error("Invalid M3 liquid classification bounds");
        above=distance>=-radius;below=distance<=radius;
        if(!allowSplit&&above&&below){above=!cameraBelow;below=cameraBelow;}
    }
    out.passes=(above?2u:0u)|(below?4u:0u);out.clip=above&&below;return out;
}
}
