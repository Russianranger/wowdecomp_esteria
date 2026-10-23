#pragma once
#include "Scene.h"
namespace ASECM3 {
struct EmissionPoint {
    Vec3 position,normal;
    uint32_t division=0,region=0,triangle=0;
    double barycentric[3]={};
};
// Render-independent snapshot of an emitter's explicitly referenced, posed
// mesh faces. Positions and face normals are in the SceneFrame model space.
// No visibility/material/bone-name heuristic selects an emission region.
class MeshEmissionSurface final {
    struct Face {
        Vec3 positions[3],normal;
        double red[3]={},maximumRed=0,cumulative=0;
        uint32_t region=0,triangle=0;
    };
    std::vector<Face> m_faces;
    double m_area=0,m_weightedArea=0,m_proposal=0;
public:
    MeshEmissionSurface(const EffectDefinition& emitter,const SceneFrame& frame);
    size_t Faces()const{return m_faces.size();}
    double Area()const{return m_area;}
    double WeightedArea()const{return m_weightedArea;}
    // Caller owns deterministic random state. False for zero emission density
    // or bounded rejection exhaustion; never substitutes an emitter origin.
    bool Sample(uint32_t& randomState,EmissionPoint& output)const;
};
}
