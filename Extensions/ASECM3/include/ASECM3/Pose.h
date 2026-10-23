#pragma once
#include "Animation.h"
namespace ASECM3 {
// Column-major affine matrices; independent of engine math types.
struct Matrix {
    double values[16]={};
    double& At(unsigned row,unsigned col) { return values[col*4+row]; }
    double At(unsigned row,unsigned col) const { return values[col*4+row]; }
    static Matrix Identity();
};
Matrix Multiply(const Matrix& left,const Matrix& right);
Matrix InverseAffine(const Matrix& value);
Matrix Compose(const KeyValue& translation,const KeyValue& rotation,const KeyValue& scale);
Vec3 TransformPoint(const Matrix& matrix,const Vec3& point);
struct LocalTransform {
    KeyValue translation{},rotation{0,0,0,1},scale{1,1,1,0};
};
struct Pose {
    std::vector<LocalTransform> local;
    std::vector<Matrix> world,skin,normal;
    double maximumDefaultBindError=0;
};
// Build the hierarchy after interpolating source-local TRS. Zero/reflected
// scale stays representable; no matrix inversion/decomposition is required.
Pose PoseFromLocalTransforms(const ModelData& model,std::vector<LocalTransform> local);
Pose BlendLocalPoses(const ModelData& model,const std::vector<LocalTransform>& from,Pose target,double amount);
// The model and bank must come from the same Document. External bank binding is
// a separate operation requiring ownership, topology and bind checks.
// The timestamp is the source M3 timeline time, not a WoW animation number.
// Concurrent/global clocks must be provided by the eventual runtime layer.
Pose EvaluatePoseAtSourceTime(const ModelData& model,const AnimationBank& bank,
    int32_t group=-1,int32_t sourceTime=0);
struct SkinnedRegion {
    uint32_t division=0,region=0;
    std::vector<Vec3> positions,normals;
    std::vector<Vec3> tangents;
    std::vector<float> tangentSigns;
};
// Regions have separate palettes; shared source vertices cannot be skinned once
// globally using an arbitrary region's lookup table.
std::vector<SkinnedRegion> SkinModel(const ModelData& model,const Pose& pose);
}
