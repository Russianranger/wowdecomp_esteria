#include "GxGeometryTransform.h"
#include "ASECM3/Document.h"
#include <cmath>
#include <algorithm>
#include <limits>
namespace ASECM3::Client {
namespace {
C3Vector Unit(C3Vector v){const double n=std::sqrt(double(v.x)*v.x+double(v.y)*v.y+double(v.z)*v.z);if(!std::isfinite(n))throw Error("M3 world direction is nonfinite");return n>1e-12?C3Vector(float(v.x/n),float(v.y/n),float(v.z/n)):C3Vector(0,0,0);}
}
GxGeometryTransform::GxGeometryTransform(const C44Matrix& matrix,bool cullMirrored):m_point(matrix),m_cullMirrored(cullMirrored){
 const auto* values=reinterpret_cast<const float*>(&matrix);for(unsigned i=0;i<16;++i)if(!std::isfinite(values[i]))throw Error("M3 world transform contains a nonfinite element");
 if(std::abs(matrix.a3)>1e-6f||std::abs(matrix.b3)>1e-6f||std::abs(matrix.c3)>1e-6f||std::abs(matrix.d3-1)>1e-6f)throw Error("M3 world geometry transform must be affine");
 const auto determinant=matrix.Determinant();if(!std::isfinite(determinant)||std::abs(determinant)<1e-10f)throw Error("M3 world geometry transform is singular");
 m_mirrored=determinant<0;m_normal=matrix.Inverse(determinant).Transpose();
}
GxGeometryTransform::GxGeometryTransform(const C44Matrix& matrix):GxGeometryTransform(matrix,matrix.Determinant()<0){}
C3Vector GxGeometryTransform::Position(const C3Vector& source)const{const auto p=source*m_point;if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))throw Error("M3 transformed position is nonfinite");return p;}
CAaBox GxGeometryTransform::Bounds(const CAaBox& source)const{
 const float values[]={source.b.x,source.b.y,source.b.z,source.t.x,source.t.y,source.t.z};
 for(float value:values)if(!std::isfinite(value))throw Error("M3 source bounds are nonfinite");
 if(source.b.x>source.t.x||source.b.y>source.t.y||source.b.z>source.t.z)throw Error("M3 source bounds are inverted");
 CAaBox out;
 for(unsigned i=0;i<8;++i){const auto p=Position(C3Vector(i&1?source.t.x:source.b.x,i&2?source.t.y:source.b.y,i&4?source.t.z:source.b.z));
  if(!i)out.b=out.t=p;else{out.b.x=std::min(out.b.x,p.x);out.b.y=std::min(out.b.y,p.y);out.b.z=std::min(out.b.z,p.z);out.t.x=std::max(out.t.x,p.x);out.t.y=std::max(out.t.y,p.y);out.t.z=std::max(out.t.z,p.z);}}
 return out;
}
CAaSphere GxGeometryTransform::EnclosingSphere(const CAaBox& source){
 double radiusSq=0;CAaSphere out;
 const float bottom[]={source.b.x,source.b.y,source.b.z},top[]={source.t.x,source.t.y,source.t.z};
 float* center[]={&out.c.x,&out.c.y,&out.c.z};
 for(unsigned i=0;i<3;++i){if(!std::isfinite(bottom[i])||!std::isfinite(top[i])||bottom[i]>top[i])throw Error("Invalid M3 sphere bounds");
  *center[i]=float((double(bottom[i])+top[i])*.5);const double half=std::max(double(top[i])-*center[i],double(*center[i])-bottom[i]);radiusSq+=half*half;}
 const double radius=std::sqrt(radiusSq);if(radius>std::numeric_limits<float>::max())throw Error("M3 bound radius overflow");
 // Round outward so the enclosing sphere cannot exclude an AABB corner.
 out.r=std::nextafter(float(radius),std::numeric_limits<float>::infinity());if(!std::isfinite(out.r))throw Error("M3 bound radius cannot be represented");return out;
}
C3Vector GxGeometryTransform::Normal(const C3Vector& source)const{return Unit(TransformDirection(source,m_normal));}
C3Vector GxGeometryTransform::Tangent(const C3Vector& source,const C3Vector& normal)const{auto t=TransformDirection(source,m_point);const auto dot=t.x*normal.x+t.y*normal.y+t.z*normal.z;return Unit(C3Vector(t.x-dot*normal.x,t.y-dot*normal.y,t.z-dot*normal.z));}
}
