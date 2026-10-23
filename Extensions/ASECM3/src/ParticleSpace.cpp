#include "ASECM3/ParticleSpace.h"
#include <algorithm>
#include <cmath>
namespace ASECM3 {
namespace {
void Finite(Vec3 v){if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))throw Error("Non-finite M3 particle space value");}
Vec3 Direction(const Matrix& m,Vec3 v){return {float(m.At(0,0)*v.x+m.At(0,1)*v.y+m.At(0,2)*v.z),float(m.At(1,0)*v.x+m.At(1,1)*v.y+m.At(1,2)*v.z),float(m.At(2,0)*v.x+m.At(2,1)*v.y+m.At(2,2)*v.z)};}
}
double ParticleUniformScale(const Matrix& m){
    for(auto value:m.values)if(!std::isfinite(value))throw Error("Non-finite M3 particle space transform");
    if(std::abs(m.At(3,0))+std::abs(m.At(3,1))+std::abs(m.At(3,2))>1e-8||std::abs(m.At(3,3)-1)>1e-8)throw Error("M3 particle space transform is not affine");
    double lengths[3]={};
    for(unsigned c=0;c<3;++c){for(unsigned r=0;r<3;++r)lengths[c]+=m.At(r,c)*m.At(r,c);lengths[c]=std::sqrt(lengths[c]);}
    const double scale=(lengths[0]+lengths[1]+lengths[2])/3;
    if(!std::isfinite(scale)||scale<1e-8)throw Error("M3 particle space transform is singular");
    for(auto length:lengths)if(std::abs(length-scale)>scale*1e-5)throw Error("M3 billboards require uniform model/camera scale");
    for(unsigned a=0;a<3;++a)for(unsigned b=a+1;b<3;++b){double dot=0;for(unsigned r=0;r<3;++r)dot+=m.At(r,a)*m.At(r,b);if(std::abs(dot)>scale*scale*1e-5)throw Error("M3 billboards do not support skewed transforms");}
    return scale;
}
std::vector<ParticleState> CameraParticles(const std::vector<ParticleState>& particles,const Matrix& view){
    if(particles.size()>16384)throw Error("M3 world particle snapshot exceeds 16384 particles");
    const double scale=ParticleUniformScale(view);auto out=particles;
    for(auto& p:out){p.position=TransformPoint(view,p.position);p.velocity=Direction(view,p.velocity);p.visual.size=float(p.visual.size*scale);Finite(p.position);Finite(p.velocity);if(!std::isfinite(p.visual.size)||p.visual.size<0)throw Error("Invalid M3 world particle size");}
    return out;
}
bool ParticleSourceBounds(const std::vector<ParticleState>& particles,const Matrix& world,Vec3& lower,Vec3& upper){
    if(particles.size()>16384)throw Error("M3 world particle bounds exceed 16384 particles");
    const double scale=ParticleUniformScale(world);const auto inverse=InverseAffine(world);bool found=false;
    for(const auto& p:particles){
        if(!std::isfinite(p.visual.size)||p.visual.size<0||!std::isfinite(p.visual.color[3]))throw Error("Invalid M3 particle bound size/alpha");
        if(p.visual.size==0||p.visual.color[3]<=0)continue;
        const auto center=TransformPoint(inverse,p.position);Finite(center);
        const double radius=double(p.visual.size)*std::sqrt(.5)/scale;
        const Vec3 lo{float(center.x-radius),float(center.y-radius),float(center.z-radius)},hi{float(center.x+radius),float(center.y+radius),float(center.z+radius)};Finite(lo);Finite(hi);
        if(!found){lower=lo;upper=hi;found=true;}else{lower={std::min(lower.x,lo.x),std::min(lower.y,lo.y),std::min(lower.z,lo.z)};upper={std::max(upper.x,hi.x),std::max(upper.y,hi.y),std::max(upper.z,hi.z)};}
    }
    return found;
}
}
