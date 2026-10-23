#include "ASECM3/RibbonTrail.h"
#include <algorithm>
#include <cmath>
namespace ASECM3 {
namespace {
constexpr size_t MaxPoints=384;
double Number(const MaterialRecord& record,const char* field,double fallback){
    const auto* p=record.Find(field);if(!p)return fallback;
    return std::visit([](auto v){return double(v);},p->value);
}
Vec3 Add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3 Sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vec3 Mul(Vec3 a,double s){return {float(a.x*s),float(a.y*s),float(a.z*s)};}
double Dot(Vec3 a,Vec3 b){return double(a.x)*b.x+double(a.y)*b.y+double(a.z)*b.z;}
Vec3 Cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
double Length(Vec3 a){return std::sqrt(Dot(a,a));}
bool Finite(Vec3 a){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
// The bank's UInt32 track for a field, by the group's collection priority (ties go to the later collection).
uint32_t Unsigned(const AnimationBank& bank,int32_t group,const MaterialRecord& record,const char* field,int32_t time,uint32_t fallback){
    const auto* initial=record.Find(std::string(field)+".default");
    if(initial){if(const auto* v=std::get_if<uint64_t>(&initial->value))fallback=uint32_t(*v);}
    const auto* id=record.Find(std::string(field)+".header.id");const auto* value=id?std::get_if<uint64_t>(&id->value):nullptr;
    if(!value||group<0||size_t(group)>=bank.groups.size())return fallback;
    const UnsignedTrack* selected=nullptr;uint32_t priority=0;
    for(auto index:bank.groups[group].collections){
        if(index>=bank.collections.size())continue;
        const auto& collection=bank.collections[index];const auto found=collection.unsignedValues.find(uint32_t(*value));
        if(found!=collection.unsignedValues.end()&&(!selected||collection.priority>=priority)){selected=&found->second;priority=collection.priority;}
    }
    return selected?SampleUnsigned(*selected,time,fallback):fallback;
}
// base -> mid over [0,mid], mid -> tip over [mid,1]
template<class T> T Curve(const T& base,const T& mid,const T& tip,double age,double midPoint){
    const double m=std::clamp(midPoint,0.01,0.999);
    if(age<=m){const double t=age/m;return base+(mid-base)*float(t);}
    const double t=(age-m)/(1-m);return mid+(tip-mid)*float(t);
}
struct Color4{float v[4];Color4 operator+(const Color4& o)const{return {{v[0]+o.v[0],v[1]+o.v[1],v[2]+o.v[2],v[3]+o.v[3]}};}
    Color4 operator-(const Color4& o)const{return {{v[0]-o.v[0],v[1]-o.v[1],v[2]-o.v[2],v[3]-o.v[3]}};}
    Color4 operator*(float s)const{return {{v[0]*s,v[1]*s,v[2]*s,v[3]*s}};}};
}
RibbonTrail::RibbonTrail(std::shared_ptr<const Scene> scene,uint32_t ribbon):m_scene(std::move(scene)),m_ribbon(ribbon){
    if(!m_scene||ribbon>=m_scene->Effects().ribbons.size())throw Error("M3 ribbon owner is unavailable");
}
void RibbonTrail::Advance(uint64_t elapsedMs,int32_t group,int32_t sourceTime,const Matrix& boneToWorld){
    const auto* bank=m_scene->EffectAnimation();if(!bank)return;
    const auto& record=m_scene->Effects().ribbons[m_ribbon].record;
    MaterialEvaluator values(*bank,group,sourceTime);
    const double now=double(elapsedMs),dt=std::max(0.0,(now-m_lastMs)/1000.0);m_lastMs=now;
    m_scale=std::sqrt(boneToWorld.At(0,0)*boneToWorld.At(0,0)+boneToWorld.At(1,0)*boneToWorld.At(1,0)+boneToWorld.At(2,0)*boneToWorld.At(2,0));
    if(!(m_scale>1e-6)||!std::isfinite(m_scale))m_scale=1;
    const double lifespan=std::max(0.05,double(values.Scalar(record,"lifespan",1)));
    const double speed=double(values.Scalar(record,"speed",0))*m_scale;
    // Bone origin and the direction its points travel (the bone's Z axis).
    const Vec3 origin{float(boneToWorld.At(0,3)),float(boneToWorld.At(1,3)),float(boneToWorld.At(2,3))};
    Vec3 axis{float(boneToWorld.At(0,2)),float(boneToWorld.At(1,2)),float(boneToWorld.At(2,2))};
    const double axisLength=Length(axis);axis=axisLength>1e-9?Mul(axis,1/axisLength):Vec3{0,0,1};
    m_active=Unsigned(*bank,group,record,"active",sourceTime,0)!=0;
    // Points drift along their direction and expire with age.
    for(auto& p:m_points){p.position=Add(p.position,Mul(p.direction,speed*dt));}
    m_points.erase(std::remove_if(m_points.begin(),m_points.end(),[&](const Point& p){return (now-p.birthMs)/1000.0>lifespan||!Finite(p.position);}),m_points.end());
    if(m_active&&Finite(origin)){
        m_head={now,origin,axis};m_hasHead=true;
        const double spacing=std::clamp(lifespan*1000.0/std::max(4.0,Number(record,"divisions",20)),8.0,120.0);
        if(now-m_lastEmitMs>=spacing){m_points.push_back(m_head);m_lastEmitMs=now;}
    }else m_hasHead=false;
    // The source `length` caps the trail's span.
    const double maxLength=double(values.Scalar(record,"length",0))*m_scale;
    if(maxLength>0){
        double total=0;size_t keep=m_points.size();
        for(size_t i=m_points.size();i-->1;){total+=Length(Sub(m_points[i].position,m_points[i-1].position));if(total>maxLength){keep=m_points.size()-i;break;}}
        if(keep<m_points.size())m_points.erase(m_points.begin(),m_points.end()-keep);
    }
    if(m_points.size()>MaxPoints)m_points.erase(m_points.begin(),m_points.end()-MaxPoints);
}
SceneRegion RibbonTrail::Build(const Matrix& worldToCamera,int32_t group,int32_t sourceTime)const{
    SceneRegion out;out.region=UINT32_MAX;
    const auto* bank=m_scene->EffectAnimation();if(!bank)return out;
    // Newest first: the live head, then the stored points.
    std::vector<const Point*> chain;
    if(m_hasHead)chain.push_back(&m_head);
    for(size_t i=m_points.size();i-->0;)chain.push_back(&m_points[i]);
    if(chain.size()<2)return out;
    const auto& record=m_scene->Effects().ribbons[m_ribbon].record;
    MaterialEvaluator values(*bank,group,sourceTime);
    const double lifespan=std::max(0.05,double(values.Scalar(record,"lifespan",1)));
    const auto scale3=values.Sample(record,"scale",2,{1,1,1,0});
    const auto base=values.Sample(record,"color_base",4,{255,255,255,255}),mid=values.Sample(record,"color_mid",4,{255,255,255,255}),tip=values.Sample(record,"color_tip",4,{255,255,255,255});
    const double scaleMid=Number(record,"scale_anim_mid",.5),colorMid=Number(record,"color_anim_mid",.5),alphaMid=Number(record,"alpha_anim_mid",.5);
    const double newest=chain.front()->birthMs;
    std::vector<Vec3> camera;camera.reserve(chain.size());
    for(const auto* p:chain)camera.push_back(TransformPoint(worldToCamera,p->position));
    out.vertices.reserve(chain.size()*2);out.indices.reserve((chain.size()-1)*6);
    uint16_t kept=0;
    for(size_t i=0;i<chain.size();++i){
        const Vec3& position=camera[i];
        // Tangent along the trail, from the neighbours; a zero-length step reuses the previous side.
        const Vec3& before=camera[i>0?i-1:i];const Vec3& after=camera[i+1<chain.size()?i+1:i];
        Vec3 tangent=Sub(before,after);double tl=Length(tangent);
        if(tl<1e-7)continue;tangent=Mul(tangent,1/tl);
        Vec3 side=Cross(tangent,position);double sl=Length(side);
        if(sl<1e-9)continue;side=Mul(side,1/sl);
        const double age=std::clamp((newest-chain[i]->birthMs)/1000.0/lifespan,0.0,1.0);
        // The head is the ribbon's base; the oldest point its tip.
        const float width=std::max(0.0f,Curve<float>(scale3[0],scale3[1],scale3[2],age,scaleMid))*float(m_scale);
        Color4 c=Curve<Color4>({{base[0],base[1],base[2],base[3]}},{{mid[0],mid[1],mid[2],mid[3]}},{{tip[0],tip[1],tip[2],tip[3]}},age,colorMid);
        const float alpha=Curve<float>(base[3],mid[3],tip[3],age,alphaMid);
        for(unsigned side_i=0;side_i<2;++side_i){
            const float sgn=side_i?1.0f:-1.0f;
            SceneVertex v;v.position=Add(position,Mul(side,sgn*width*.5));
            v.normal={0,0,-1};v.tangent=tangent;v.tangentSign=1;
            for(unsigned k=0;k<3;++k)v.color[k]=uint8_t(std::lround(std::clamp(c.v[k],0.0f,255.0f)));
            v.color[3]=uint8_t(std::lround(std::clamp(alpha,0.0f,255.0f)));
            const float u=float(age),vv=side_i?1.0f:0.0f;
            v.uv={u,vv,u,vv};out.vertices.push_back(std::move(v));
        }
        if(kept>0){const uint16_t b=uint16_t((kept-1)*2);for(uint16_t idx:{b,uint16_t(b+1),uint16_t(b+2),uint16_t(b+1),uint16_t(b+3),uint16_t(b+2)})out.indices.push_back(idx);}
        ++kept;
        if(out.vertices.size()>=65000)break;
    }
    if(out.indices.empty()){out.vertices.clear();}
    return out;
}
bool RibbonTrail::WorldBounds(Vec3& lower,Vec3& upper)const{
    if(m_points.empty()&&!m_hasHead)return false;
    bool first=true;
    const auto take=[&](const Vec3& p){if(first){lower=upper=p;first=false;return;}lower={std::min(lower.x,p.x),std::min(lower.y,p.y),std::min(lower.z,p.z)};upper={std::max(upper.x,p.x),std::max(upper.y,p.y),std::max(upper.z,p.z)};};
    for(const auto& p:m_points)take(p.position);
    if(m_hasHead)take(m_head.position);
    const float pad=float(m_scale)*1.0f;
    lower={lower.x-pad,lower.y-pad,lower.z-pad};upper={upper.x+pad,upper.y+pad,upper.z+pad};
    return true;
}
}
