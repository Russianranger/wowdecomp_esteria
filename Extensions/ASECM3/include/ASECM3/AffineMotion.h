#pragma once
#include "Pose.h"
namespace ASECM3 {
// Observed affine endpoints: shortest-arc proper rotation and linear symmetric
// stretch/translation. Unlike matrix-entry interpolation, rigid rotations do
// not shrink the collision volume. No extrapolation or solver rewind.
class AffineMotion final {
    struct Parts {double rotation[4]={},stretch[9]={};int sign=1;};
    Matrix m_first,m_last;
    Parts m_from,m_to;
    static Parts Decode(const Matrix& matrix);
public:
    AffineMotion(const Matrix& first,const Matrix& last);
    Matrix At(double weight)const;
};
}
