#pragma once
#include "Document.h"
namespace ASECM3 {
struct PickRay {Vec3 origin,direction;double maximum=0;};
// Double-sided positive triangle intersection. Direction is not normalized:
// affine inverse transforms preserve the caller's distance parameter.
bool IntersectTriangle(Vec3 a,Vec3 b,Vec3 c,const PickRay& ray,double maximum,double& distance);
}
