#include "ASECM3/Picking.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
using namespace ASECM3;
void Require(bool condition,const char* message){if(!condition)throw Error(message);}
std::vector<uint8_t> Read(const char* path){const auto size=std::filesystem::file_size(path);if(size>128u*1024*1024)throw Error("Picking input exceeds 128 MiB");std::vector<uint8_t> bytes(size);std::ifstream file(path,std::ios::binary);if(!file.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw Error("Short picking input read");return bytes;}
SceneRegion Triangle(float z){SceneRegion r;for(const auto p:{Vec3{-1,-1,z},Vec3{1,-1,z},Vec3{0,1,z}}){SceneVertex v;v.position=p;r.vertices.push_back(v);}r.indices={0,1,2};return r;}
unsigned Fixtures(){
    unsigned checks=0;PickingIndex index;PickHit hit;SceneFrame frame;frame.regions={Triangle(2),Triangle(4)};frame.regions[1].region=1;index.Refit(frame);
    const auto check=[&](PickRay ray,std::vector<bool> eligible,bool expected,double distance=0){const bool found=index.Intersect(frame,eligible,ray,hit);Require(found==expected,"Picking fixture hit/miss mismatch");if(found)Require(std::abs(hit.distance-distance)<1e-6,"Picking fixture distance mismatch");++checks;};
    check({{0,0,0},{0,0,1},10},{true,true},true,2);
    check({{0,0,6},{0,0,-1},10},{true,true},true,2);
    check({{0,0,0},{0,0,1},10},{false,true},true,4);
    check({{0,0,0},{0,0,1},10},{false,false},false);
    check({{0,0,0},{0,0,1},1.99},{true,true},false);
    check({{0,0,0},{0,0,2},10},{true,true},true,1);
    check({{3,0,0},{0,0,1},10},{true,true},false);
    check({{0,0,0},{1,0,0},10},{true,true},false);
    check({{0,0,0},{0,0,-1},10},{true,true},false);
    check({{0,0,2},{0,0,1},10},{true,true},true,0);
    check({{-1,-1,0},{0,0,1},10},{true,true},true,2);
    for(auto& v:frame.regions[0].vertices)v.position.z=3;index.Refit(frame);
    check({{0,0,0},{0,0,1},10},{true,true},true,3);
    unsigned rejected=0;
    try{index.Intersect(frame,{true,true},{{0,0,0},{0,0,0},10},hit);}catch(const Error&){++rejected;}
    try{index.Intersect(frame,{true},{{0,0,0},{0,0,1},10},hit);}catch(const Error&){++rejected;}
    try{index.Intersect(frame,{true,true},{{0,0,0},{0,0,1},std::numeric_limits<double>::infinity()},hit);}catch(const Error&){++rejected;}
    frame.regions[0].indices[0]=65535;try{index.Refit(frame);}catch(const Error&){++rejected;}
    Require(rejected==4,"Picking invalid input rejection failed");
    MaterialCatalog catalog;catalog.materials.resize(2);catalog.materials[0].type=catalog.materials[1].type=1;
    catalog.materials[0].record.properties.push_back({"flags","uint32",uint64_t(0x40)});
    catalog.batches.resize(2);catalog.batches[1].region=1;catalog.batches[1].material=1;
    std::vector<MaterialPass> passes(2);passes[0].material=0;passes[0].batch=0;passes[1].material=1;passes[1].batch=1;passes[1].blend=1;
    const auto eligible=PickableRegions(frame,catalog,passes,{true,true},0);
    Require(!eligible[0]&&eligible[1],"Source NoHitTest mask was ignored");++checks;
    Require(!PickableRegions(frame,catalog,passes,{true,false},0)[1],"Hidden render region picked");++checks;
    Require(!PickableRegions(frame,catalog,passes,{true,true},1)[1],"Opaque native mode accepted transparent region");++checks;
    Require(PickableRegions(frame,catalog,passes,{true,true},2)[1],"Transparent native mode rejected transparent region");++checks;
    passes[1].opacity=0;Require(!PickableRegions(frame,catalog,passes,{true,true},0)[1],"Zero-opacity composite child picked");++checks;
    return checks+rejected;
}
unsigned Sample(const SceneFrame& frame){
    PickingIndex index;index.Refit(frame);unsigned count=0;
    for(size_t ri=0;ri<frame.regions.size();++ri){const auto& r=frame.regions[ri];if(r.indices.empty())continue;
        for(size_t ti=0;ti+2<r.indices.size();ti+=3){
            const auto a=r.vertices[r.indices[ti]].position,b=r.vertices[r.indices[ti+1]].position,c=r.vertices[r.indices[ti+2]].position;
            const double ux=double(b.x)-a.x,uy=double(b.y)-a.y,uz=double(b.z)-a.z,vx=double(c.x)-a.x,vy=double(c.y)-a.y,vz=double(c.z)-a.z;
            double nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;const auto length=std::sqrt(nx*nx+ny*ny+nz*nz);if(length<1e-10)continue;nx/=length;ny/=length;nz/=length;
            const Vec3 center{float((double(a.x)+b.x+c.x)/3),float((double(a.y)+b.y+c.y)/3),float((double(a.z)+b.z+c.z)/3)};
            const PickRay ray{{float(center.x+nx),float(center.y+ny),float(center.z+nz)},{float(-nx),float(-ny),float(-nz)},2};
            std::vector<bool> eligible(frame.regions.size(),false);eligible[ri]=true;PickHit hit;
            Require(index.Intersect(frame,eligible,ray,hit),"Actual model triangle centroid ray missed");
            Require(hit.frameRegion==ri&&hit.distance<=1.001,"Actual model nearest hit mismatch");
            eligible[ri]=false;Require(!index.Intersect(frame,eligible,ray,hit),"Hidden actual model region picked");++count;break;
        }
    }
    return count;
}
int main(int argc,char** argv){try{
    if(argc<2||argc>3)throw Error("Usage: ASECM3PickingInspect model.m3 [bank.m3a]");
    const auto fixtures=Fixtures();auto scene=Scene::Parse(Read(argv[1]));if(argc==3)scene=scene->WithAnimationBank(Read(argv[2]),argv[2]);
    unsigned samples=Sample(scene->EvaluateGroup()),poses=1;
    for(uint32_t i=0;i<scene->Model().animations.size();++i){if(scene->SequenceGroup(i)<0)continue;const auto& clip=scene->Model().animations[i];samples+=Sample(scene->EvaluateSequence(i,(clip.end-clip.start)/2));++poses;}
    std::cout<<"{\"ok\":true,\"fixtures\":"<<fixtures<<",\"poses\":"<<poses<<",\"regionSamples\":"<<samples<<",\"scope\":\"Portable triangle/visibility/refit checks; not live native owner or cursor selection\"}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
