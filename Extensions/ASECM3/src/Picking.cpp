#include "ASECM3/Picking.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace ASECM3 {
namespace {
struct D3 {double x,y,z;};
D3 Point(Vec3 p){return {p.x,p.y,p.z};}
D3 Sub(D3 a,D3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
D3 Cross(D3 a,D3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
double Dot(D3 a,D3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
bool Finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
bool Box(Vec3 lower,Vec3 upper,const PickRay& ray,double maximum){
    double near=0,far=maximum;
    const double origin[]={ray.origin.x,ray.origin.y,ray.origin.z},dir[]={ray.direction.x,ray.direction.y,ray.direction.z};
    const double lo[]={lower.x,lower.y,lower.z},hi[]={upper.x,upper.y,upper.z};
    for(unsigned axis=0;axis<3;++axis){
        if(dir[axis]==0){if(origin[axis]<lo[axis]||origin[axis]>hi[axis])return false;continue;}
        double a=(lo[axis]-origin[axis])/dir[axis],b=(hi[axis]-origin[axis])/dir[axis];if(a>b)std::swap(a,b);
        near=std::max(near,a);far=std::min(far,b);if(near>far)return false;
    }
    return true;
}
}
bool IntersectTriangle(Vec3 a,Vec3 b,Vec3 c,const PickRay& ray,double maximum,double& distance){
    const auto edge1=Sub(Point(b),Point(a)),edge2=Sub(Point(c),Point(a));
    const auto p=Cross(Point(ray.direction),edge2);const double determinant=Dot(edge1,p);
    // Relative tolerance: small legitimate models/rays must remain pickable.
    const double scale=std::sqrt(Dot(edge1,edge1)*Dot(p,p));
    if(!std::isfinite(scale)||std::abs(determinant)<=scale*1e-12||scale==0)return false;
    const auto offset=Sub(Point(ray.origin),Point(a));
    const double u=Dot(offset,p)/determinant;if(u<0||u>1)return false;
    const auto q=Cross(offset,edge1);const double v=Dot(Point(ray.direction),q)/determinant;
    if(v<0||u+v>1)return false;
    distance=Dot(edge2,q)/determinant;
    // Match the native geometric test: both windings, nearest positive hit.
    return std::isfinite(distance)&&distance>=0&&distance<=maximum;
}
std::vector<bool> PickableRegions(const SceneFrame& frame,const MaterialCatalog& catalog,
    const std::vector<MaterialPass>& passes,const std::vector<bool>& visible,uint32_t nativeMode){
    if(visible.size()!=frame.regions.size())throw Error("M3 picking visibility count mismatch");
    std::vector<bool> eligible(frame.regions.size(),false);
    for(const auto& pass:passes){
        if(!std::isfinite(pass.opacity))throw Error("M3 picking material opacity is nonfinite");
        if(pass.opacity<=0)continue;
        const auto& batch=catalog.batches.at(pass.batch);const auto& material=catalog.materials.at(pass.material);
        // The 0x40 meaning belongs to the standard-material schema. Never
        // reinterpret arbitrary material classes' bits using that layout.
        const auto* standard=material.type==1?&material:material.bufferSurface.get();
        if(standard){const auto* field=standard->record.Find("flags");
            if(field&&(std::visit([](auto value){return uint32_t(value);},field->value)&0x40))continue;}
        if((nativeMode==1&&pass.blend!=0)||(nativeMode==2&&pass.blend==0))continue;
        for(size_t i=0;i<frame.regions.size();++i)if(visible[i]&&frame.regions[i].division==batch.division&&frame.regions[i].region==batch.region)eligible[i]=true;
    }
    return eligible;
}
void PickingIndex::Refit(const SceneFrame& frame){
    m_blocks.clear();m_regions=frame.regions.size();
    try{for(uint32_t ri=0;ri<frame.regions.size();++ri){const auto& region=frame.regions[ri];
        if(region.indices.size()%3)throw Error("M3 picking region has incomplete triangle");
        for(size_t first=0;first<region.indices.size();first+=96){
            Block block;block.region=ri;block.first=uint32_t(first);block.count=uint32_t(std::min(size_t(96),region.indices.size()-first));
            bool found=false;
            for(size_t i=first;i<first+block.count;++i){
                const auto index=region.indices[i];if(index>=region.vertices.size())throw Error("M3 picking index exceeds region vertices");
                const auto p=region.vertices[index].position;if(!Finite(p))throw Error("M3 picking vertex is nonfinite");
                if(!found){block.lower=block.upper=p;found=true;}else{
                    block.lower.x=std::min(block.lower.x,p.x);block.lower.y=std::min(block.lower.y,p.y);block.lower.z=std::min(block.lower.z,p.z);
                    block.upper.x=std::max(block.upper.x,p.x);block.upper.y=std::max(block.upper.y,p.y);block.upper.z=std::max(block.upper.z,p.z);}
            }
            m_blocks.push_back(block);
        }
    }}catch(...){m_blocks.clear();m_regions=0;throw;}
}
bool PickingIndex::Intersect(const SceneFrame& frame,const std::vector<bool>& eligible,const PickRay& ray,PickHit& hit)const{
    if(frame.regions.size()!=m_regions||eligible.size()!=m_regions)throw Error("M3 picking frame/visibility mismatch");
    if(!Finite(ray.origin)||!Finite(ray.direction)||!std::isfinite(ray.maximum)||ray.maximum<0||
       (ray.direction.x==0&&ray.direction.y==0&&ray.direction.z==0))throw Error("Invalid M3 picking ray");
    bool found=false;double maximum=ray.maximum;
    for(const auto& block:m_blocks){
        if(!eligible[block.region]||!Box(block.lower,block.upper,ray,maximum))continue;
        const auto& region=frame.regions[block.region];
        if(size_t(block.first)+block.count>region.indices.size())throw Error("M3 picking topology changed without refit");
        for(uint32_t i=block.first;i<block.first+block.count;i+=3){
            const auto a=region.indices[i],b=region.indices[i+1],c=region.indices[i+2];
            if(std::max({a,b,c})>=region.vertices.size())throw Error("M3 picking triangle exceeds region vertices");
            double distance;if(IntersectTriangle(region.vertices[a].position,region.vertices[b].position,region.vertices[c].position,ray,maximum,distance)){
                found=true;maximum=distance;hit={distance,block.region,i/3};
            }
        }
    }
    return found;
}
}
