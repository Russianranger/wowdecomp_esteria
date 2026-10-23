#include "ASECM3/PrimitiveEmission.h"
#include "ASECM3/ParticleSpace.h"
#include <algorithm>
#include <cmath>
namespace ASECM3 {
namespace {
uint32_t UInt(const MaterialRecord& record,const char* field){
    const auto* p=record.Find(field);const auto* v=p?std::get_if<uint64_t>(&p->value):nullptr;
    if(!v||*v>UINT32_MAX)throw Error(std::string("Invalid primitive emission field: ")+field);
    return uint32_t(*v);
}
double Random(uint32_t& state){if(!state)state=0x6d2b79f5u;state^=state<<13;state^=state>>17;state^=state<<5;return double(state)/4294967296.0;}
Vec3 SphereDirection(uint32_t& state){
    const double z=2*Random(state)-1,angle=6.283185307179586*Random(state),r=std::sqrt(std::max(0.0,1-z*z));
    return {float(r*std::cos(angle)),float(r*std::sin(angle)),float(z)};
}
double Radius(const MaterialRecord& record,const MaterialEvaluator& values,const char* field){
    const auto* initial=record.Find(std::string(field)+".default");
    if(!initial||initial->kind!="float"||!record.Find(std::string(field)+".header.id"))throw Error("Missing primitive emission radius reference");
    const double v=values.Scalar(record,field,0);
    if(v<0||v>1e6)throw Error("Primitive emission radius is outside [0,1000000]");
    return v;
}
}
void ValidatePrimitiveEmission(const EffectDefinition& emitter){
    // Shapes: 0 point, 1 plane, 2 sphere, 3 cuboid, 4 cylinder, 5 disc. Directions: 0 directional, 1 radial, 2 along the
    // bone's Z axis, 3 random. Spline shapes and mesh-normal directions remain unsupported.
    const auto shape=UInt(emitter.record,"emit_shape"),type=UInt(emitter.record,"emit_type");
    if(shape>5)throw Error("Primitive emission requires a Point, Plane, Sphere, Cuboid, Cylinder or Disc shape");
    if(type>3)throw Error("Primitive emission requires a Directional, Radial, Z-axis or Random direction");
}
bool SamplePrimitiveEmission(const EffectDefinition& emitter,const MaterialEvaluator& values,const Matrix& bone,uint32_t& random,EmissionPoint& output){
    ValidatePrimitiveEmission(emitter);
    // The observed bone lane currently supports an orthogonal uniform scale.
    // Never silently normalize away a skew or a collapsed source transform.
    const double scale=ParticleUniformScale(bone);
    const auto& record=emitter.record;const auto shape=UInt(record,"emit_shape"),type=UInt(record,"emit_type");
    Vec3 position{},radial{};
    const auto size=[&](){const auto v=values.Sample(record,"emit_shape_size",2,{1,1,1,0});for(unsigned i=0;i<3;++i)if(!std::isfinite(v[i])||v[i]<0||v[i]>1e6)throw Error("Primitive emission size is outside [0,1000000]");return v;};
    if(shape==0)radial=SphereDirection(random);
    else if(shape==1||shape==3){
        // Plane (x,y) and cuboid (x,y,z): uniform over the box centred on the bone.
        const auto v=size();
        position={float((Random(random)-.5)*v[0]),float((Random(random)-.5)*v[1]),shape==3?float((Random(random)-.5)*v[2]):0.0f};
        radial=position;const double l=std::sqrt(double(radial.x)*radial.x+double(radial.y)*radial.y+double(radial.z)*radial.z);
        radial=l>1e-9?Vec3{float(radial.x/l),float(radial.y/l),float(radial.z/l)}:SphereDirection(random);
    }else{
        const double outer=Radius(record,values,"emit_shape_radius");
        const double inner=(UInt(record,"flags")&0x10)?Radius(record,values,"emit_shape_radius_cutout"):0;
        if(inner>outer)throw Error("Primitive emission cutout exceeds outer radius");
        if(outer==0||inner==outer)return false;
        double radius=0;
        if(shape==2){
            radial=SphereDirection(random);
            radius=std::cbrt(inner*inner*inner+Random(random)*(outer*outer*outer-inner*inner*inner));
        }else{
            const double angle=6.283185307179586*Random(random);
            radial={float(std::cos(angle)),float(std::sin(angle)),0};
            radius=std::sqrt(inner*inner+Random(random)*(outer*outer-inner*inner));
        }
        position={float(radial.x*radius),float(radial.y*radius),float(radial.z*radius)};
        // Cylinder: a disc swept along Z over the shape's height.
        if(shape==4){const auto v=size();position.z=float((Random(random)-.5)*v[2]);}
    }
    Vec3 direction=radial;
    if(type==2)direction={0,0,1};
    else if(type==3)direction=SphereDirection(random);
    else if(type==0){
        // Directional: along +Z within a cone; the source spreads are full angles in radians.
        const double spreadX=std::clamp(double(values.Scalar(record,"emit_spread_x",0)),0.0,6.283185307179586),spreadY=std::clamp(double(values.Scalar(record,"emit_spread_y",0)),0.0,6.283185307179586);
        const double yaw=(Random(random)-.5)*spreadX,pitch=(Random(random)-.5)*spreadY;
        direction={float(std::sin(yaw)*std::cos(pitch)),float(std::sin(pitch)),float(std::cos(yaw)*std::cos(pitch))};
    }
    EmissionPoint result;result.division=result.region=result.triangle=UINT32_MAX;
    result.position=TransformPoint(bone,position);
    result.normal={float((bone.At(0,0)*direction.x+bone.At(0,1)*direction.y+bone.At(0,2)*direction.z)/scale),float((bone.At(1,0)*direction.x+bone.At(1,1)*direction.y+bone.At(1,2)*direction.z)/scale),float((bone.At(2,0)*direction.x+bone.At(2,1)*direction.y+bone.At(2,2)*direction.z)/scale)};
    for(const auto value:{result.position.x,result.position.y,result.position.z,result.normal.x,result.normal.y,result.normal.z})if(!std::isfinite(value))throw Error("Non-finite primitive emission sample");
    output=result;return true;
}
}
