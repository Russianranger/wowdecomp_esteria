#pragma once
#include "ParticleSpace.h"
namespace ASECM3 {
// One observed actor-update interval. Translation/uniform scale interpolate
// linearly; orientation follows the shortest quaternion arc. No extrapolation.
class ParticleTransformInterval final {
    struct Transform {KeyValue translation{},rotation{0,0,0,1};double scale=1;};
    double m_begin=0,m_end=0;
    Matrix m_first,m_last;
    Transform m_from,m_to;
    static Transform Decode(const Matrix& matrix);
public:
    ParticleTransformInterval(double begin,double end,const Matrix& first,const Matrix& last);
    Matrix At(double elapsedMs)const;
};
}
