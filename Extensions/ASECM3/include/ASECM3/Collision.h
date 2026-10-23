#pragma once
#include "Pose.h"
#include "Ray.h"
namespace ASECM3 {
struct CollisionMesh {std::vector<Vec3> vertices;std::vector<uint16_t> indices;};
struct HitVolume {
    uint32_t kind=0;int32_t bone=-1;Matrix local=Matrix::Identity();
    Vec3 size;CollisionMesh mesh;
};
struct CollisionData {
    CollisionMesh mesh;HitVolume tight;
    // Fuzzy source volumes are retained separately. They are not physical
    // blockers and are never substituted for an absent tight collision volume.
    std::vector<HitVolume> fuzzy;
    std::string error;
};
CollisionData DecodeCollision(const Document& document,const ModelData& model);
bool CollisionBounds(const CollisionData& data,const Pose& pose,Vec3& lower,Vec3& upper);
bool IntersectCollision(const CollisionData& data,const Pose& pose,const PickRay& ray,double& distance);
}
