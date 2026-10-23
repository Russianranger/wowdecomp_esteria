#pragma once
#include "EmissionSurface.h"
namespace ASECM3 {
// Two observed solved meshes. Vertices interpolate linearly in model space;
// sampling uses the interpolated triangle's area, normal and source red density.
// No physics state is advanced or rewound, and no extrapolation is permitted.
class EmissionSurfaceInterval final {
    struct Face {
        Vec3 first[3],last[3];
        double red[3]={},maximumRed=0,areaBound=0,cumulative=0;
        uint32_t region=0,triangle=0;
    };
    std::vector<Face> m_faces;
    double m_begin=0,m_end=0,m_proposal=0;
public:
    EmissionSurfaceInterval(const EffectDefinition& emitter,double begin,double end,
                           const SceneFrame& first,const SceneFrame& last);
    size_t Faces()const{return m_faces.size();}
    bool Sample(double elapsedMs,uint32_t& randomState,EmissionPoint& output)const;
};
}
