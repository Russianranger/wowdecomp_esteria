#include "ASECM3/EmissionSurfaceInterval.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
namespace ASECM3 {
namespace {
struct D3 { double x,y,z; };
D3 Point(const Vec3& p){return {p.x,p.y,p.z};}
D3 Add(D3 a,D3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
D3 Sub(D3 a,D3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
D3 Scale(D3 a,double s){return {a.x*s,a.y*s,a.z*s};}
D3 Cross(D3 a,D3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
double Length(D3 a){return std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z);}
bool Finite(const Vec3& p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
double Random(uint32_t& state){if(!state)state=0x6d2b79f5u;state^=state<<13;state^=state>>17;state^=state<<5;return double(state)*(1.0/4294967296.0);}
using Regions=std::unordered_map<uint32_t,const SceneRegion*>;
Regions Index(const SceneFrame& frame){
    Regions regions;
    for(const auto& region:frame.regions)if(region.division==0 && !regions.emplace(region.region,&region).second)throw Error("M3 emission history has duplicate region identities");
    return regions;
}
}
EmissionSurfaceInterval::EmissionSurfaceInterval(const EffectDefinition& emitter,double begin,double end,const SceneFrame& first,const SceneFrame& last)
    :m_begin(begin),m_end(end){
    if(!std::isfinite(begin)||!std::isfinite(end)||begin<0||end<begin)throw Error("Invalid M3 emission surface interval");
    const auto* property=emitter.record.Find("emit_shape");const auto* shape=property?std::get_if<uint64_t>(&property->value):nullptr;
    if(!shape || *shape!=7)throw Error("M3 emission history requires the source Mesh emission shape");
    if(emitter.meshRegions.empty())throw Error("M3 mesh emitter has no explicit region references");
    const auto from=Index(first),to=Index(last);size_t visited=0;
    for(auto index:emitter.meshRegions){
        const auto a=from.find(index),b=to.find(index);
        if(a==from.end() || b==to.end())throw Error("M3 emission region is missing from solved mesh history");
        const auto& old=*a->second;const auto& now=*b->second;
        if(old.indices!=now.indices || old.vertices.size()!=now.vertices.size())throw Error("M3 emission topology changes within observed history");
        if(now.indices.size()%3)throw Error("M3 emission history has a partial triangle");
        if(now.indices.size()/3>250000-visited)throw Error("M3 emission history exceeds 250000 faces");
        visited+=now.indices.size()/3;
        for(size_t i=0;i<now.indices.size();i+=3){
            Face face;face.region=index;face.triangle=uint32_t(i/3);
            for(unsigned j=0;j<3;++j){
                const auto vertex=now.indices[i+j];
                if(vertex>=now.vertices.size())throw Error("M3 emission history index exceeds region vertices");
                const auto& before=old.vertices[vertex];const auto& after=now.vertices[vertex];
                if(!Finite(before.position)||!Finite(after.position))throw Error("M3 emission history has a non-finite position");
                if(before.color[0]!=after.color[0])throw Error("M3 emission density changes within observed history");
                face.first[j]=before.position;face.last[j]=after.position;face.red[j]=double(after.color[0])/255;
            }
            // For linear vertices, cross(edge1(t),edge2(t))=C0+C1*t+C2*t*t.
            // Triangle inequality gives a conservative area proposal for all
            // t in [0,1], including faces degenerate at both observed endpoints.
            const auto u=Sub(Point(face.first[1]),Point(face.first[0])),v=Sub(Point(face.first[2]),Point(face.first[0]));
            const auto du=Sub(Sub(Point(face.last[1]),Point(face.last[0])),u),dv=Sub(Sub(Point(face.last[2]),Point(face.last[0])),v);
            face.areaBound=.5*(Length(Cross(u,v))+Length(Add(Cross(u,dv),Cross(du,v)))+Length(Cross(du,dv)))*(1+1e-12);
            if(!std::isfinite(face.areaBound))throw Error("M3 emission history area bound overflow");
            face.maximumRed=std::max({face.red[0],face.red[1],face.red[2]});
            if(face.areaBound==0 || face.maximumRed==0)continue;
            m_proposal+=face.areaBound*face.maximumRed;
            if(!std::isfinite(m_proposal))throw Error("M3 emission history proposal overflow");
            face.cumulative=m_proposal;m_faces.push_back(face);
        }
    }
}
bool EmissionSurfaceInterval::Sample(double time,uint32_t& random,EmissionPoint& output)const{
    if(!std::isfinite(time)||time<m_begin||time>m_end)throw Error("M3 particle birth is outside observed mesh history");
    if(m_faces.empty() || m_proposal<=0)return false;
    const double weight=time==m_end?1:time==m_begin?0:(time-m_begin)/(m_end-m_begin);
    for(unsigned attempt=0;attempt<64;++attempt){
        const double target=Random(random)*m_proposal;
        auto found=std::upper_bound(m_faces.begin(),m_faces.end(),target,[](double value,const Face& face){return value<face.cumulative;});
        if(found==m_faces.end())found=std::prev(m_faces.end());const auto& face=*found;
        D3 positions[3];for(unsigned j=0;j<3;++j)positions[j]=Add(Scale(Point(face.first[j]),1-weight),Scale(Point(face.last[j]),weight));
        const auto cross=Cross(Sub(positions[1],positions[0]),Sub(positions[2],positions[0]));const double length=Length(cross);
        const double root=std::sqrt(Random(random)),v=Random(random),barycentric[3]={1-root,root*(1-v),root*v};
        double red=0;for(unsigned j=0;j<3;++j)red+=barycentric[j]*face.red[j];
        const double acceptance=std::min(1.0,(length*.5/face.areaBound)*(red/face.maximumRed));
        if(length==0 || Random(random)>=acceptance)continue;
        EmissionPoint point;point.region=face.region;point.triangle=face.triangle;
        point.normal={float(cross.x/length),float(cross.y/length),float(cross.z/length)};
        D3 position{};for(unsigned j=0;j<3;++j){point.barycentric[j]=barycentric[j];position=Add(position,Scale(positions[j],barycentric[j]));}
        point.position={float(position.x),float(position.y),float(position.z)};
        if(!Finite(point.position)||!Finite(point.normal))throw Error("M3 interpolated emission point is not finite");
        output=point;return true;
    }
    return false;
}
}
