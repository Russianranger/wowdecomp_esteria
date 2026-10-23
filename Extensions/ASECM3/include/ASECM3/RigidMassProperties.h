#pragma once
#include "Pose.h"
class btCompoundShape;
namespace ASECM3 {
struct RigidMassProperties {Matrix centerInBone;Vec3 inertia;double mass=0;};
// Recenter a privately owned Bullet compound into its principal mass frame.
// The caller must multiply the body's world pose by centerInBone, transform
// joint frames by its inverse, and undo it before publishing skeletal poses.
RigidMassProperties RecenterRigidCompound(btCompoundShape& compound,const std::vector<double>& childMasses);
}
