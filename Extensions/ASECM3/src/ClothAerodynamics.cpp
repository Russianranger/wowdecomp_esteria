#include "ASECM3/ClothAerodynamics.h"
#include <cmath>
#include <limits>
namespace ASECM3 {
namespace {
struct D3 {double x,y,z;};
D3 Sub(D3 a,D3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
D3 Scale(D3 a,double s){return {a.x*s,a.y*s,a.z*s};}
double Dot(D3 a,D3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
D3 Cross(D3 a,D3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
D3 Read(Vec3 v){if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z))throw Error("Nonfinite cloth aerodynamic vector");return {v.x,v.y,v.z};}
}
Vec3 ClothAirImpulse(const std::array<Vec3,3>& positions,const std::array<Vec3,3>& velocities,
    const std::array<double,3>& inverseMass,Vec3 airVelocity,double density,double drag,double lift,double dt){
    for(auto value:{density,drag,lift,dt})if(!std::isfinite(value)||value<0)throw Error("Invalid cloth aerodynamic coefficient or interval");
    D3 p[3],v[3];double effectiveInverseMass=0;
    for(unsigned j=0;j<3;++j){p[j]=Read(positions[j]);v[j]=Read(velocities[j]);if(!std::isfinite(inverseMass[j])||inverseMass[j]<0)throw Error("Invalid cloth aerodynamic inverse mass");effectiveInverseMass+=inverseMass[j]/9;}
    const auto air=Read(airVelocity);
    if(dt==0||density==0||(drag==0&&lift==0)||effectiveInverseMass==0)return {};
    const auto areaNormal=Cross(Sub(p[1],p[0]),Sub(p[2],p[0]));
    const double twiceArea=std::sqrt(Dot(areaNormal,areaNormal));
    const D3 relative{(v[0].x+v[1].x+v[2].x)/3-air.x,(v[0].y+v[1].y+v[2].y)/3-air.y,(v[0].z+v[1].z+v[2].z)/3-air.z};
    const double speed=std::sqrt(Dot(relative,relative));
    if(twiceArea==0||speed==0)return {};
    const auto normal=Scale(areaNormal,1/twiceArea),direction=Scale(relative,1/speed);
    const double cosine=Dot(direction,normal),pressureArea=.25*density*twiceArea;
    // F_drag = .5*rho*A*Cd*|v|^2*|cos(theta)|. With frozen
    // geometry the quadratic drag ODE has this positive, non-reversing step.
    const double dragStep=effectiveInverseMass*pressureArea*drag*std::abs(cosine)*speed*dt;
    const double liftStep=effectiveInverseMass*pressureArea*lift*cosine*speed*dt;
    if(!std::isfinite(pressureArea)||!std::isfinite(dragStep)||!std::isfinite(liftStep))throw Error("Cloth aerodynamic step exceeds finite domain");
    const double finalSpeed=speed/(1+dragStep);
    // Lift is orthogonal to relative flow. Normalize its provisional direction
    // to preserve speed; explicit Euler lift would manufacture kinetic energy.
    const auto perpendicular=Sub(normal,Scale(direction,cosine));
    const auto provisional=Sub(direction,Scale(perpendicular,liftStep));
    const double norm=std::sqrt(Dot(provisional,provisional));
    if(!std::isfinite(norm)||norm==0)throw Error("Cloth aerodynamic lift direction exceeds finite domain");
    const auto result=Scale(Sub(Scale(provisional,finalSpeed/norm),relative),1/effectiveInverseMass);
    for(auto value:{result.x,result.y,result.z})if(!std::isfinite(value)||std::abs(value)>std::numeric_limits<float>::max())throw Error("Cloth aerodynamic impulse exceeds finite domain");
    return {float(result.x),float(result.y),float(result.z)};
}
}
