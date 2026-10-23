#pragma once
#include "Engine/Source/Tempest/C44Matrix.h"
#include "Engine/Source/Tempest/CAaBox.h"
#include "Engine/Source/Tempest/CAaSphere.h"
namespace ASECM3::Client {
// Engine row-vector affine transform. World callers supply camera-relative
// model-to-camera coordinates explicitly; no auto-fit or source-unit guessing.
class GxGeometryTransform final {
    C44Matrix m_point,m_normal;
    bool m_mirrored=false;
    bool m_cullMirrored=false;
public:
    explicit GxGeometryTransform(const C44Matrix& modelToCamera,bool cullMirrored);
    explicit GxGeometryTransform(const C44Matrix& modelToCamera);
    CAaBox Bounds(const CAaBox& source) const;
    static CAaSphere EnclosingSphere(const CAaBox& source);
    C3Vector Position(const C3Vector& source)const;
    C3Vector Normal(const C3Vector& source)const;
    C3Vector Tangent(const C3Vector& source,const C3Vector& normal)const;
    bool Mirrored()const{return m_mirrored;}
    bool CullMirrored()const{return m_cullMirrored;}
};
}
