#include "ASECM3/Scene.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace ASECM3;
void Require(bool value,const char* message){if(!value)throw Error(message);}
std::vector<uint8_t> Read(const char* path){const auto size=std::filesystem::file_size(path);if(size>128u*1024*1024)throw Error("Collision input exceeds 128 MiB");std::vector<uint8_t> bytes(size);std::ifstream file(path,std::ios::binary);if(!file.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw Error("Short collision read");return bytes;}
unsigned Fixtures(){
    CollisionData data;Pose pose;pose.world={Matrix::Identity()};data.tight.bone=0;data.tight.size={1,2,3};unsigned count=0;
    const auto check=[&](PickRay ray,bool expected,double expectedDistance=0){double distance=-1;const bool hit=IntersectCollision(data,pose,ray,distance);Require(hit==expected,"Collision fixture hit/miss mismatch");if(hit)Require(std::abs(distance-expectedDistance)<1e-6,"Collision fixture distance mismatch");++count;};
    check({{-5,0,0},{1,0,0},10},true,4);check({{0,0,0},{1,0,0},10},true,1);check({{-5,3,0},{1,0,0},10},false);check({{-5,0,0},{1,0,0},3.9},false);
    data.tight.kind=1;check({{-5,0,0},{1,0,0},10},true,4);check({{0,0,0},{0,0,2},10},true,.5);check({{-5,1,0},{1,0,0},10},true,5);check({{0,0,5},{0,0,-1},3.9},false);
    data.tight.kind=2;data.tight.size={1,4,0};check({{0,0,-5},{0,0,1},10},true,2);check({{-5,0,0},{1,0,0},10},true,4);check({{0,0,2.5f},{0,0,1},10},true,.5);check({{-5,0,2},{1,0,0},10},true,4);check({{0,0,0},{0,0,1},10},true,3);check({{2,0,-5},{0,0,1},10},false);
    data.tight.kind=3;check({{0,0,-5},{0,0,1},10},true,3);check({{0,0,0},{0,0,1},10},true,2);check({{-5,0,0},{1,0,0},10},true,4);check({{-5,0,2.5f},{1,0,0},10},false);
    data.tight.kind=2;data.tight.size.y=0;check({{0,0,-5},{0,0,1},10},true,4);
    data.tight.kind=4;data.tight.mesh.vertices={{-1,-1,2},{1,-1,2},{0,1,2}};data.tight.mesh.indices={0,1,2};check({{0,0,0},{0,0,1},10},true,2);
    data.mesh=data.tight.mesh;data.tight.bone=-1;check({{0,0,0},{0,0,1},10},true,2);data.mesh={};check({{0,0,0},{0,0,1},10},false);
    data.fuzzy.push_back(data.tight);data.fuzzy[0].bone=0;check({{0,0,0},{0,0,1},10},false); // Fuzzy is not collision.
    data.tight.bone=0;data.tight.kind=1;data.tight.size={1,0,0};data.tight.local.At(0,0)=2;pose.world[0].At(0,3)=10;
    check({{0,0,0},{1,0,0},20},true,8);check({{10,0,0},{2,0,0},20},true,1);
    Vec3 lower,upper;Require(CollisionBounds(data,pose,lower,upper)&&lower.x==8&&upper.x==12&&lower.y==-1&&upper.z==1,"Collision affine bounds mismatch");++count;
    data.tight.local.At(0,0)=-2;check({{0,0,0},{1,0,0},20},true,8); // Reflection retains the same surface distance.
    data.tight.local=Matrix::Identity();data.tight.size.x=0;check({{0,0,0},{1,0,0},20},false);
    data.tight.size.x=1;data.error="invalid source fixture";check({{0,0,0},{1,0,0},20},false);data.error.clear();
    bool badRay=false;try{double t;IntersectCollision(data,pose,{{0,0,0},{0,0,0},10},t);}catch(const Error&){badRay=true;}Require(badRay,"Zero direction collision ray accepted");++count;
    data.tight.bone=1;bool invalid=false;try{check({{0,0,0},{1,0,0},20},false);}catch(const Error&){invalid=true;}Require(invalid,"Invalid collision bone accepted");++count;
    return count;
}
void Volume(const HitVolume& v){std::cout<<"{\"shape\":"<<v.kind<<",\"bone\":"<<v.bone<<",\"size\":["<<v.size.x<<','<<v.size.y<<','<<v.size.z<<"],\"matrix\":[";for(unsigned i=0;i<16;++i){if(i)std::cout<<',';std::cout<<v.local.values[i];}std::cout<<"],\"vertices\":"<<v.mesh.vertices.size()<<",\"indices\":"<<v.mesh.indices.size()<<'}';}
int main(int argc,char** argv){try{
    if(argc<2||argc>3)throw Error("Usage: ASECM3CollisionInspect model.m3 [bank.m3a]");const auto fixtures=Fixtures();auto scene=Scene::Parse(Read(argv[1]));if(argc==3)scene=scene->WithAnimationBank(Read(argv[2]),argv[2]);
    const auto& data=scene->Collision();Require(data.error.empty(),data.error.c_str());unsigned poses=0,hits=0;Vec3 lower,upper;
    const auto sample=[&](const SceneFrame& frame){++poses;if(!CollisionBounds(data,frame.pose,lower,upper)){double t;Require(!IntersectCollision(data,frame.pose,{{0,0,0},{0,0,1},100},t),"Absent source collision unexpectedly hit");return;}
        const Vec3 origin{lower.x-1,float((double(lower.y)+upper.y)/2),float((double(lower.z)+upper.z)/2)};double t;
        // AABB-center ray is an inspection sample; concave custom meshes may miss.
        if(IntersectCollision(data,frame.pose,{origin,{1,0,0},double(upper.x)-lower.x+2},t)){Require(t>=0&&t<=double(upper.x)-lower.x+2,"Collision hit beyond segment");++hits;}
    };
    sample(scene->EvaluateGroup());for(uint32_t i=0;i<scene->Model().animations.size();++i)if(scene->SequenceGroup(i)>=0){const auto& clip=scene->Model().animations[i];sample(scene->EvaluateSequence(i,(clip.end-clip.start)/2));}
    std::cout.precision(9);std::cout<<"{\"ok\":true,\"fixtures\":"<<fixtures<<",\"poses\":"<<poses<<",\"boundsRayHits\":"<<hits<<",\"explicitVertices\":"<<data.mesh.vertices.size()<<",\"explicitIndices\":"<<data.mesh.indices.size()<<",\"tight\":";Volume(data.tight);std::cout<<",\"fuzzy\":[";for(size_t i=0;i<data.fuzzy.size();++i){if(i)std::cout<<',';Volume(data.fuzzy[i]);}std::cout<<"],\"scope\":\"Source decode and analytic query math, not gameplay collision or terrain contacts\"}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
