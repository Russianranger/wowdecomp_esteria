#pragma once
#include "Physics.h"
namespace ASECM3 {
struct ConvexPhysicsMesh {
    // Hull vertices centred and expressed in principal mass axes. The child
    // frame maps them back to the scale-baked source body-bone frame.
    std::vector<Vec3> vertices;
    Matrix childFrame=Matrix::Identity();
    Vec3 inertiaPerMass;
    double volume=0;
};
// PHSH kind 4 only. Source points, local affine and positive bone scale are
// baked once. No bounding-box substitute or reinterpretation of mesh kind 5.
ConvexPhysicsMesh BuildConvexPhysics(const PhysicsShape&,const Vec3& boneScale);
}
