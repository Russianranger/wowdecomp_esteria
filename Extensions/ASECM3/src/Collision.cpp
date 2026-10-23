#include "ASECM3/Collision.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace ASECM3 {
namespace {
bool Finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
void Charge(size_t bytes,size_t& total){if(bytes>32u*1024*1024-total)throw Error("M3 collision decode exceeds 32 MiB");total+=bytes;}
CollisionMesh ReadMesh(const Document& doc,Reference vertices,Reference indices,size_t& owned){
    CollisionMesh mesh;
    if(!vertices.count&&!indices.count)return mesh;
    if(!vertices.count||!indices.count||vertices.count>65536||indices.count>3000000||indices.count%3)throw Error("Invalid M3 collision mesh counts");
    Charge(size_t(vertices.count)*sizeof(Vec3)+size_t(indices.count)*sizeof(uint16_t),owned);
    mesh.vertices.reserve(vertices.count);mesh.indices.reserve(indices.count);
    for(uint32_t i=0;i<vertices.count;++i){const auto v=doc.At(vertices,i);const Vec3 p{v.Float("x"),v.Float("y"),v.Float("z")};if(!Finite(p))throw Error("Nonfinite M3 collision vertex");mesh.vertices.push_back(p);}
    const auto& section=doc.Resolve(indices,"U16_");
    for(uint32_t i=0;i<indices.count;++i){const auto index=doc.U16(section.offset+size_t(i)*2);if(index>=vertices.count)throw Error("M3 collision face references missing vertex");mesh.indices.push_back(index);}
    return mesh;
}
HitVolume ReadVolume(const Document& doc,const Record& record,const std::string& prefix,const ModelData& model,size_t& owned){
    HitVolume out;out.kind=record.UInt(prefix+"shape");out.bone=record.Int(prefix+"bone");
    if(record.UInt(prefix+"bone_fb")!=0)throw Error("Unsupported M3 hit volume bone flags");
    if(out.bone<-1||(out.bone>=0&&size_t(out.bone)>=model.skeleton.size()))throw Error("M3 hit volume bone is outside source skeleton");
    out.size={record.Float(prefix+"size0"),record.Float(prefix+"size1"),record.Float(prefix+"size2")};
    if(!Finite(out.size)||out.size.x<0||out.size.y<0||out.size.z<0)throw Error("Invalid M3 hit volume dimensions");
    for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r){const auto value=record.Float(prefix+"matrix."+"xyzw"[c]+"."+"xyzw"[r]);if(!std::isfinite(value))throw Error("Nonfinite M3 hit volume matrix");out.local.At(r,c)=value;}
    out.mesh=ReadMesh(doc,record.Ref(prefix+"vertices"),record.Ref(prefix+"face_data"),owned);
    if(out.bone>=0){if(out.kind>4)throw Error("Unsupported M3 tight/fuzzy hit volume kind");InverseAffine(out.local);if(out.kind==4&&out.mesh.indices.empty())throw Error("M3 custom hit volume has no triangles");}
    return out;
}
bool Enabled(const HitVolume& v){if(v.bone<0)return false;switch(v.kind){case 0:return v.size.x>0&&v.size.y>0&&v.size.z>0;case 1:case 2:return v.size.x>0;case 3:return v.size.x>0&&v.size.y>0;case 4:return !v.mesh.indices.empty();default:return false;}}
Matrix ShapeMatrix(const HitVolume& v,const Pose& pose){if(v.bone<0||size_t(v.bone)>=pose.world.size())throw Error("M3 collision pose is missing its source bone");return Multiply(pose.world[v.bone],v.local);}
PickRay InverseRay(const PickRay& ray,const Matrix& matrix){
    const auto inverse=InverseAffine(matrix);const auto origin=TransformPoint(inverse,ray.origin);auto rotation=inverse;for(unsigned i=0;i<3;++i)rotation.At(i,3)=0;
    return {origin,TransformPoint(rotation,ray.direction),ray.maximum};
}
void AddPoint(Vec3 p,Vec3& lower,Vec3& upper,bool& found){if(!Finite(p))throw Error("Nonfinite M3 collision bound");if(!found){lower=upper=p;found=true;}else{lower.x=std::min(lower.x,p.x);lower.y=std::min(lower.y,p.y);lower.z=std::min(lower.z,p.z);upper.x=std::max(upper.x,p.x);upper.y=std::max(upper.y,p.y);upper.z=std::max(upper.z,p.z);}}
bool MeshBounds(const CollisionMesh& mesh,Vec3& lower,Vec3& upper){bool found=false;for(auto i:mesh.indices){if(i>=mesh.vertices.size())throw Error("Invalid M3 collision bound index");AddPoint(mesh.vertices[i],lower,upper,found);}return found;}
bool MeshHit(const CollisionMesh& mesh,const PickRay& ray,double& distance){
    if(mesh.indices.size()%3)throw Error("Incomplete M3 collision triangle");bool found=false;double nearest=ray.maximum;
    for(size_t i=0;i<mesh.indices.size();i+=3){double t;if(IntersectTriangle(mesh.vertices.at(mesh.indices[i]),mesh.vertices.at(mesh.indices[i+1]),mesh.vertices.at(mesh.indices[i+2]),ray,nearest,t)){nearest=t;found=true;}}
    if(found)distance=nearest;return found;
}
template<class F>void Roots(double a,double b,double c,F accept){
    if(a==0){if(b!=0)accept(-c/(2*b));return;}
    double discriminant=b*b-a*c;if(discriminant<0)return;
    const double root=std::sqrt(discriminant),q=-b-std::copysign(root,b);
    if(q==0){accept(-b/a);return;}const double t0=q/a,t1=c/q;accept(std::min(t0,t1));accept(std::max(t0,t1));
}
bool PrimitiveHit(const HitVolume& v,const PickRay& ray,double& distance){
    const double ox=ray.origin.x,oy=ray.origin.y,oz=ray.origin.z,dx=ray.direction.x,dy=ray.direction.y,dz=ray.direction.z;
    double nearest=ray.maximum;bool found=false;
    const auto accept=[&](double t){if(std::isfinite(t)&&t>=0&&t<=nearest){nearest=t;found=true;}};
    if(v.kind==0){
        double near=-std::numeric_limits<double>::infinity(),far=std::numeric_limits<double>::infinity();
        const double o[]={ox,oy,oz},d[]={dx,dy,dz},size[]={v.size.x,v.size.y,v.size.z};
        for(unsigned i=0;i<3;++i){if(d[i]==0){if(o[i]<-size[i]||o[i]>size[i])return false;continue;}double a=(-size[i]-o[i])/d[i],b=(size[i]-o[i])/d[i];if(a>b)std::swap(a,b);near=std::max(near,a);far=std::min(far,b);if(near>far)return false;}
        accept(near);accept(far);
    }else if(v.kind==1){
        Roots(dx*dx+dy*dy+dz*dz,ox*dx+oy*dy+oz*dz,ox*ox+oy*oy+oz*oz-double(v.size.x)*v.size.x,accept);
    }else if(v.kind==2||v.kind==3){
        const double half=v.size.y*.5,radiusSq=double(v.size.x)*v.size.x;
        Roots(dx*dx+dy*dy,ox*dx+oy*dy,ox*ox+oy*oy-radiusSq,[&](double t){const double z=oz+dz*t;if(z>=-half&&z<=half)accept(t);});
        for(int side:{-1,1}){
            const double center=side*half;
            if(v.kind==2){const double z=oz-center;
                Roots(dx*dx+dy*dy+dz*dz,ox*dx+oy*dy+z*dz,ox*ox+oy*oy+z*z-radiusSq,[&](double t){if(side*(oz+dz*t-center)>=0)accept(t);});
            }else if(dz!=0){const double t=(center-oz)/dz,x=ox+dx*t,y=oy+dy*t;if(x*x+y*y<=radiusSq)accept(t);}
        }
    }
    if(found)distance=nearest;return found;
}
}
CollisionData DecodeCollision(const Document& doc,const ModelData& model){
    CollisionData out;const auto root=doc.Model();size_t owned=0;
    out.mesh=ReadMesh(doc,root.Ref("collision_verts"),root.Ref("collision_faces"),owned);
    out.tight=ReadVolume(doc,root,"hittest_tight.",model,owned);
    const auto fuzzy=root.Ref("hittests");if(fuzzy.count>4096)throw Error("M3 fuzzy volume count exceeds 4096");out.fuzzy.reserve(fuzzy.count);
    for(uint32_t i=0;i<fuzzy.count;++i)out.fuzzy.push_back(ReadVolume(doc,doc.At(fuzzy,i),"",model,owned));
    return out;
}
bool CollisionBounds(const CollisionData& data,const Pose& pose,Vec3& lower,Vec3& upper){
    if(!data.error.empty())return false;
    if(!data.mesh.indices.empty())return MeshBounds(data.mesh,lower,upper);
    const auto& v=data.tight;if(!Enabled(v))return false;Vec3 localLower,localUpper;
    if(v.kind==4){if(!MeshBounds(v.mesh,localLower,localUpper))return false;}
    else{Vec3 extent=v.size;if(v.kind==1)extent={v.size.x,v.size.x,v.size.x};if(v.kind==2||v.kind==3)extent={v.size.x,v.size.x,float(v.size.y*.5+(v.kind==2?v.size.x:0))};localLower={-extent.x,-extent.y,-extent.z};localUpper=extent;}
    const auto matrix=ShapeMatrix(v,pose);bool found=false;
    for(unsigned corner=0;corner<8;++corner)AddPoint(TransformPoint(matrix,{corner&1?localUpper.x:localLower.x,corner&2?localUpper.y:localLower.y,corner&4?localUpper.z:localLower.z}),lower,upper,found);
    return found;
}
bool IntersectCollision(const CollisionData& data,const Pose& pose,const PickRay& ray,double& distance){
    if(!Finite(ray.origin)||!Finite(ray.direction)||!std::isfinite(ray.maximum)||ray.maximum<0||(ray.direction.x==0&&ray.direction.y==0&&ray.direction.z==0))throw Error("Invalid M3 collision ray");
    if(!data.error.empty())return false;
    if(!data.mesh.indices.empty())return MeshHit(data.mesh,ray,distance);
    const auto& volume=data.tight;if(!Enabled(volume))return false;
    const auto local=InverseRay(ray,ShapeMatrix(volume,pose));
    return volume.kind==4?MeshHit(volume.mesh,local,distance):PrimitiveHit(volume,local,distance);
}
}
