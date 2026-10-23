#include "ASECM3/ClothBending.h"
#include <algorithm>
#include <cmath>
namespace ASECM3 {
namespace {
Vec3 Sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vec3 Add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3 Scale(Vec3 a,double s){return {float(a.x*s),float(a.y*s),float(a.z*s)};}
double Dot(Vec3 a,Vec3 b){return double(a.x)*b.x+double(a.y)*b.y+double(a.z)*b.z;}
Vec3 Cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
}
bool ClothBendGeometry(const std::array<Vec3,4>& p,double& angle,std::array<Vec3,4>* gradient){
    for(const auto& v:p)if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))throw Error("Nonfinite cloth bending point");
    const auto edge=Sub(p[1],p[0]);const double edge2=Dot(edge,edge);
    const auto n=Cross(edge,Sub(p[2],p[0])),m=Cross(Sub(p[3],p[0]),edge);const double n2=Dot(n,n),m2=Dot(m,m);
    if(!std::isfinite(edge2)||!std::isfinite(n2)||!std::isfinite(m2))throw Error("Cloth bending geometry exceeds finite domain");
    if(edge2<1e-20||n2<1e-20||m2<1e-20)return false;
    const auto length=std::sqrt(edge2);
    // Both atan2 arguments share |n|*|m|, which cancels. Evaluate the triple
    // product in double instead of normalizing two float vectors per hinge.
    const double sine=((double(n.y)*m.z-double(n.z)*m.y)*edge.x+(double(n.z)*m.x-double(n.x)*m.z)*edge.y+(double(n.x)*m.y-double(n.y)*m.x)*edge.z)/length;
    angle=std::atan2(sine,Dot(n,m));
    if(gradient){
        const auto c=Scale(n,-length/n2),d=Scale(m,-length/m2);
        (*gradient)[2]=c;(*gradient)[3]=d;
        (*gradient)[0]=Add(Scale(c,Dot(Sub(p[2],p[1]),edge)/edge2),Scale(d,Dot(Sub(p[3],p[1]),edge)/edge2));
        (*gradient)[1]=Add(Scale(c,Dot(Sub(p[0],p[2]),edge)/edge2),Scale(d,Dot(Sub(p[0],p[3]),edge)/edge2));
    }
    return true;
}
bool ProjectClothBend(std::array<Vec3,4>& points,const std::array<double,4>& inverseMass,double restAngle,double stiffness){
    if(!std::isfinite(restAngle)||!std::isfinite(stiffness)||stiffness<0||stiffness>1)throw Error("Invalid cloth bending coefficient");
    for(auto w:inverseMass)if(!std::isfinite(w)||w<0)throw Error("Invalid cloth bending mass");
    double angle;std::array<Vec3,4> gradient;
    if(!ClothBendGeometry(points,angle,&gradient))return false;
    double denominator=0;for(unsigned i=0;i<4;++i)denominator+=inverseMass[i]*Dot(gradient[i],gradient[i]);
    if(!std::isfinite(denominator))throw Error("Cloth bending weighted gradient exceeds finite domain");
    if(denominator<1e-20||stiffness==0)return false;
    constexpr double twoPi=6.28318530717958647692;
    const double error=std::remainder(angle-restAngle,twoPi),multiplier=-stiffness*error/denominator;
    for(unsigned i=0;i<4;++i)points[i]=Add(points[i],Scale(gradient[i],multiplier*inverseMass[i]));
    return true;
}
}
