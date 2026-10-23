#include "ASECM3/ParticleBillboards.h"
#include <algorithm>
#include <cmath>
namespace ASECM3 {
namespace {
double Dot(Vec3 a,Vec3 b){return double(a.x)*b.x+double(a.y)*b.y+double(a.z)*b.z;}
Vec3 Unit(Vec3 v){const double length=std::sqrt(Dot(v,v));if(!std::isfinite(length)||length<1e-12)throw Error("Invalid M3 particle camera basis");return {float(v.x/length),float(v.y/length),float(v.z/length)};}
void UInt(MaterialRecord& record,const char* field,uint32_t value){for(auto& p:record.properties)if(p.name==field){p.kind="uint32";p.value=uint64_t(value);return;}record.properties.push_back({field,"uint32",uint64_t(value)});}
uint32_t UInt(const MaterialRecord& record,const char* field){const auto* p=record.Find(field);if(!p)return 0;const auto* value=std::get_if<uint64_t>(&p->value);if(!value||*value>UINT32_MAX)throw Error("Invalid M3 particle material integer");return uint32_t(*value);}
}
Material ParticleSurfaceMaterial(const Material& source){
    if(source.type!=1)throw Error("M3 particle preview requires a standard source material");
    Material out=source;UInt(out.record,"flags",UInt(source.record,"flags")|1u|2u|8u);
    for(auto& layer:out.layers)if(UInt(layer.record,"uv_source")==6)UInt(layer.record,"uv_source",0);
    return out;
}
SceneRegion BuildParticleBillboards(const std::vector<ParticleState>& particles,Vec3 right,Vec3 up){
    if(particles.size()>4096)throw Error("M3 billboard batch exceeds 4096 particles");
    right=Unit(right);const double projection=Dot(right,up);
    up=Unit({float(up.x-projection*right.x),float(up.y-projection*right.y),float(up.z-projection*right.z)});
    const Vec3 normal=Unit({right.y*up.z-right.z*up.y,right.z*up.x-right.x*up.z,right.x*up.y-right.y*up.x});
    SceneRegion out;out.region=UINT32_MAX;out.vertices.reserve(particles.size()*4);out.indices.reserve(particles.size()*6);
    for(const auto& p:particles){
        const auto& visual=p.visual;
        if(!std::isfinite(visual.size)||visual.size<0||!std::isfinite(visual.rotation))throw Error("Invalid M3 billboard size/rotation");
        if(!std::isfinite(visual.u0)||!std::isfinite(visual.v0)||!std::isfinite(visual.u1)||!std::isfinite(visual.v1)||visual.u0<0||visual.v0<0||visual.u1>1||visual.v1>1||visual.u0>visual.u1||visual.v0>visual.v1)throw Error("Invalid M3 billboard atlas rectangle");
        if(visual.size==0||visual.color[3]<=0)continue;
        const float cs=std::cos(visual.rotation),sn=std::sin(visual.rotation),half=visual.size*.5f;
        const Vec3 x{(right.x*cs+up.x*sn)*half,(right.y*cs+up.y*sn)*half,(right.z*cs+up.z*sn)*half};
        const Vec3 y{(up.x*cs-right.x*sn)*half,(up.y*cs-right.y*sn)*half,(up.z*cs-right.z*sn)*half};
        const uint16_t base=uint16_t(out.vertices.size());
        for(unsigned i=0;i<4;++i){
            const float sx=i==1||i==2?1.0f:-1.0f,sy=i>=2?1.0f:-1.0f;
            SceneVertex v;v.position={p.position.x+sx*x.x+sy*y.x,p.position.y+sx*x.y+sy*y.y,p.position.z+sx*x.z+sy*y.z};v.normal=normal;v.tangent={right.x*cs+up.x*sn,right.y*cs+up.y*sn,right.z*cs+up.z*sn};v.tangentSign=1;
            if(!std::isfinite(v.position.x)||!std::isfinite(v.position.y)||!std::isfinite(v.position.z))throw Error("Non-finite M3 billboard vertex");
            for(unsigned c=0;c<4;++c){if(!std::isfinite(visual.color[c])||visual.color[c]<0||visual.color[c]>1)throw Error("Invalid M3 billboard color");v.color[c]=uint8_t(std::lround(visual.color[c]*255));}
            v.uv={sx>0?visual.u1:visual.u0,sy>0?visual.v1:visual.v0};out.vertices.push_back(std::move(v));
        }
        for(uint16_t index:{uint16_t(0),uint16_t(1),uint16_t(2),uint16_t(0),uint16_t(2),uint16_t(3)})out.indices.push_back(base+index);
    }
    return out;
}
}
