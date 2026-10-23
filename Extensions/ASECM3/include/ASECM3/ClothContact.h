#pragma once
#include "Document.h"
#include <cmath>
#include <initializer_list>
namespace ASECM3 {
// Only for an active, position-corrected contact. Zero restitution sets the
// particle's normal speed to the moving surface's; frictionless tangent stays.
inline Vec3 ConstrainClothContactVelocity(Vec3 velocity,Vec3 normal,Vec3 surface){
    for(const auto value:{velocity,normal,surface})if(!std::isfinite(value.x)||!std::isfinite(value.y)||!std::isfinite(value.z))throw Error("Nonfinite cloth contact velocity");
    const double n2=double(normal.x)*normal.x+double(normal.y)*normal.y+double(normal.z)*normal.z;
    if(n2<1e-20)throw Error("Cloth contact normal is degenerate");
    const double speed=((double(velocity.x)-surface.x)*normal.x+(double(velocity.y)-surface.y)*normal.y+(double(velocity.z)-surface.z)*normal.z)/n2;
    const Vec3 out{float(velocity.x-normal.x*speed),float(velocity.y-normal.y*speed),float(velocity.z-normal.z*speed)};
    if(!std::isfinite(out.x)||!std::isfinite(out.y)||!std::isfinite(out.z))throw Error("Cloth contact response exceeds finite domain");
    return out;
}
}
