#include "ASECM3/EmissionSurface.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
namespace ASECM3 {
namespace {
bool Finite(const Vec3& p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
double Random(uint32_t& state){
    // xorshift32; zero is replaced so the caller cannot enter its fixed point.
    if(!state)state=0x6d2b79f5u;
    state^=state<<13;state^=state>>17;state^=state<<5;
    return double(state)*(1.0/4294967296.0);
}
}
MeshEmissionSurface::MeshEmissionSurface(const EffectDefinition& emitter,const SceneFrame& frame){
    const auto* property=emitter.record.Find("emit_shape");
    const auto* shape=property?std::get_if<uint64_t>(&property->value):nullptr;
    if(!shape||*shape!=7)throw Error("M3 emission surface requires the source Mesh emission shape");
    if(emitter.meshRegions.empty())throw Error("M3 mesh emitter has no explicit region references");
    std::unordered_map<uint32_t,const SceneRegion*> regions;
    for(const auto& region:frame.regions)if(region.division==0){
        if(!regions.emplace(region.region,&region).second)throw Error("M3 emission frame has duplicate region identities");
    }
    size_t visited=0;
    for(auto index:emitter.meshRegions){
        const auto found=regions.find(index);
        if(found==regions.end())throw Error("M3 emission region is missing from the posed frame");
        const auto& region=*found->second;
        if(region.indices.size()%3)throw Error("M3 emission region has a partial triangle");
        if(region.indices.size()/3>250000-visited)throw Error("M3 emission surface exceeds 250000 faces");
        visited+=region.indices.size()/3;
        for(size_t i=0;i<region.indices.size();i+=3){
            Face face;face.region=index;face.triangle=uint32_t(i/3);
            for(unsigned j=0;j<3;++j){
                if(region.indices[i+j]>=region.vertices.size())throw Error("M3 emission face index exceeds region vertices");
                const auto& vertex=region.vertices[region.indices[i+j]];
                if(!Finite(vertex.position))throw Error("M3 emission face has a non-finite position");
                face.positions[j]=vertex.position;face.red[j]=double(vertex.color[0])/255.0;
            }
            const auto& a=face.positions[0];const auto& b=face.positions[1];const auto& c=face.positions[2];
            const double ux=double(b.x)-a.x,uy=double(b.y)-a.y,uz=double(b.z)-a.z;
            const double vx=double(c.x)-a.x,vy=double(c.y)-a.y,vz=double(c.z)-a.z;
            const double nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
            const double length=std::sqrt(nx*nx+ny*ny+nz*nz);
            if(!std::isfinite(length))throw Error("M3 emission face area is not finite");
            if(length==0)continue;
            const double area=length*.5;
            m_area+=area;
            m_weightedArea+=area*(face.red[0]+face.red[1]+face.red[2])/3;
            face.maximumRed=std::max({face.red[0],face.red[1],face.red[2]});
            if(face.maximumRed==0)continue;
            face.normal={float(nx/length),float(ny/length),float(nz/length)};
            m_proposal+=area*face.maximumRed;face.cumulative=m_proposal;
            if(!std::isfinite(m_proposal)||!std::isfinite(m_area)||!std::isfinite(m_weightedArea))throw Error("M3 emission surface area overflow");
            m_faces.push_back(face);
        }
    }
}
bool MeshEmissionSurface::Sample(uint32_t& state,EmissionPoint& output)const{
    if(m_faces.empty()||m_proposal<=0)return false;
    // Face proposal is area*maximum vertex red. Rejection by interpolated
    // red/maxRed produces density proportional to red over the entire mesh.
    // Each proposed face accepts with probability >=1/3, barring rounding.
    for(unsigned attempt=0;attempt<64;++attempt){
        const double target=Random(state)*m_proposal;
        auto found=std::upper_bound(m_faces.begin(),m_faces.end(),target,[](double value,const Face& face){return value<face.cumulative;});
        if(found==m_faces.end())found=std::prev(m_faces.end());
        const auto& face=*found;
        const double root=std::sqrt(Random(state)),v=Random(state);
        const double weights[3]={1-root,root*(1-v),root*v};
        double red=0;for(unsigned j=0;j<3;++j)red+=weights[j]*face.red[j];
        if(Random(state)*face.maximumRed>=red)continue;
        EmissionPoint point;point.region=face.region;point.triangle=face.triangle;point.normal=face.normal;
        double x=0,y=0,z=0;
        for(unsigned j=0;j<3;++j){point.barycentric[j]=weights[j];x+=weights[j]*face.positions[j].x;y+=weights[j]*face.positions[j].y;z+=weights[j]*face.positions[j].z;}
        point.position={float(x),float(y),float(z)};output=point;return true;
    }
    return false;
}
}
