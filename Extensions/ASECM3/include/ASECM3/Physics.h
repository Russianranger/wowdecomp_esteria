#pragma once
#include "Pose.h"
namespace ASECM3 {
// Source-owned physics description. This is solver input, not a claim that a
// PHRB flag or an undecoded cloth coefficient has been simulated correctly.
struct PhysicsShape {
    uint32_t version=0,kind=0;
    Matrix local;
    float size[3]{};
    std::vector<Vec3> vertices;
    std::vector<uint16_t> faces;
    std::vector<uint8_t> raw;
};
struct PhysicsBody {
    uint32_t version=0,bone=0,flags=0,priority=0;
    int32_t simulationType=-1; // PHRB v2 does not specify this field
    uint32_t physicalMaterial=0,localForces=0,worldForces=0;
    float mass=0,friction=0,bounce=0,linearDamping=0,angularDamping=0,gravityFactor=0;
    bool hasActiveState=false;
    uint32_t activeStateId=0,activeStateDefault=0,activeStateNull=0,activeStateInterpolation=0,activeStateFlags=0;
    float blendOut=0;
    std::vector<PhysicsShape> shapes;
    std::vector<uint8_t> raw;
};
struct PhysicsJoint {
    uint32_t kind=0,endpoint1=0,endpoint2=0;
    Matrix frame1,frame2;
    bool limit=false,frictionEnabled=false;
    float minimum=0,maximum=0,angle=0,friction=0,dampingRatio=0,angularFrequency=0,breakThreshold=0;
    int32_t collisionValue=0;
    std::vector<uint8_t> raw;
};
struct PhysicsPayload {
    uint32_t section=0,count=0,version=0;
    std::string tag;
    std::vector<uint8_t> bytes;
};
struct ClothConstraint {
    Matrix local;
    float radius=0,height=0;
    int32_t bone=-1; // source PHCC value; solver must establish index domain
};
struct ClothInfluence {
    uint32_t influencedRegion=0,simulationRegion=0;
    std::vector<uint64_t> lookups; // retain packed values; no guessed interpolation
    std::vector<uint32_t> weights;
};
struct PhysicsCloth {
    uint32_t version=0,simulationRegion=0;
    std::vector<uint16_t> skinBones;
    std::vector<uint8_t> simulated;
    std::vector<uint32_t> vertexBones,vertexWeights;
    std::vector<ClothConstraint> constraints;
    std::vector<ClothInfluence> influences;
    std::vector<std::pair<std::string,float>> coefficients;
    bool hasLocalWind=false;
    Vec3 localWind;
};
struct PhysicsData {
    std::vector<PhysicsBody> bodies;
    std::vector<PhysicsJoint> joints;
    std::vector<PhysicsCloth> cloths;
    // Preserve all referenced physics/cloth buffers, including unknown fields,
    // once per source section. No pointers into Document or engine objects.
    std::vector<PhysicsPayload> payloads;
    uint32_t clothCount=0,constraintCount=0;
    // Validated PHCL/PHAC helpers in primary division only. Keep their data for physics.
    std::vector<uint32_t> hiddenSimulationRegions;
    std::vector<std::string> visibilityWarnings;
    std::string error;
};
PhysicsData DecodePhysics(const Document& document,const ModelData& model);
}
