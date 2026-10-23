#pragma once
#include "Engine/Source/Tempest/C3Vector.h"
#include <array>
namespace ASECM3::Client {
// Owned values in the same camera space as submitted vertices. No native light
// pointers survive the draw, and preview callers supply no environment.
struct SceneEnvironment {
    struct PointLight {C3Vector position,color,attenuation;};
    C3Vector ambient,diffuse,specular,towardSun,fogColor;
    std::array<PointLight,4> points{};
    unsigned pointCount=0;
    float fogStart=0,fogEnd=0,fogDensity=0,fogDistanceScale=1;
    float opacity=1;
    bool fogEnabled=false;
    std::array<float,4> clipPlane{};
    bool clipEnabled=false;
};
}
