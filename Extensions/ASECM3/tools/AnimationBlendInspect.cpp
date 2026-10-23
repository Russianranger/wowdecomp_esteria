#include "ASECM3/Scene.h"
#include "ASECM3/ClothSimulation.h"
#include "ASECM3/Semantics.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
using namespace ASECM3;
namespace {
void Require(bool condition,const char* text){if(!condition)throw Error(text);}
std::vector<uint8_t> Read(const std::filesystem::path& path){
    const auto size=std::filesystem::file_size(path);Require(size<=128ull*1024*1024,"Inspection source exceeds128MiB");
    std::vector<uint8_t> data(size,0);std::ifstream file(path,std::ios::binary);
    Require(bool(file.read(reinterpret_cast<char*>(data.data()),size)),"Short inspection read");return data;
}
double Difference(const Pose& a,const Pose& b){
    Require(a.world.size()==b.world.size(),"Pose domain mismatch");double error=0;
    for(size_t i=0;i<a.world.size();++i)for(unsigned j=0;j<16;++j){Require(std::isfinite(a.world[i].values[j])&&std::isfinite(b.world[i].values[j]),"Nonfinite transition matrix");error=std::max(error,std::abs(a.world[i].values[j]-b.world[i].values[j]));}
    return error;
}
void Near(Vec3 a,Vec3 b){Require(std::abs(a.x-b.x)+std::abs(a.y-b.y)+std::abs(a.z-b.z)<1e-5,"Transition numeric mismatch");}
unsigned Fixtures(){
    ModelData model;model.skeleton.resize(2);model.skeleton[1].parent=0;
    for(auto& bone:model.skeleton)for(unsigned i=0;i<16;++i)bone.inverseBind[i]=i%5==0?1.f:0.f;
    std::vector<LocalTransform> a(2),b(2);a[1].translation[0]=b[1].translation[0]=1;b[0].rotation={0,0,1,0};
    auto target=PoseFromLocalTransforms(model,b);const auto start=PoseFromLocalTransforms(model,a);
    Near(TransformPoint(BlendLocalPoses(model,a,target,.5).world[1],{}),{0,1,0});
    Require(Difference(BlendLocalPoses(model,a,target,0),start)==0,"Start endpoint moved");
    Require(Difference(BlendLocalPoses(model,a,target,1),target)==0,"End endpoint moved");
    auto midpoint=BlendLocalPoses(model,a,target,.5);auto interrupted=BlendLocalPoses(model,midpoint.local,start,0);
    Require(Difference(midpoint,interrupted)<1e-6,"Interrupted transition snapped");
    b=a;b[0].rotation={0,0,0,-1};Near(TransformPoint(BlendLocalPoses(model,a,PoseFromLocalTransforms(model,b),.5).world[1],{}),{1,0,0});
    constexpr double pi=3.14159265358979323846;
    a[0].rotation={0,0,float(std::sin(170*pi/360)),float(std::cos(170*pi/360))};
    b[0].rotation={0,0,float(std::sin(-170*pi/360)),float(std::cos(-170*pi/360))};
    Near(TransformPoint(BlendLocalPoses(model,a,PoseFromLocalTransforms(model,b),.5).world[1],{}),{-1,0,0});
    a[0].rotation=b[0].rotation={0,0,0,1};a[0].scale={-1,2,3,0};b[0].scale={1,4,5,0};
    const auto zero=BlendLocalPoses(model,a,PoseFromLocalTransforms(model,b),.5);
    Near(TransformPoint(zero.world[0],{1,1,1}),{0,3,4});
    for(auto v:zero.normal[0].values)Require(std::isfinite(v),"Zero scale transition normal is nonfinite");
    unsigned rejected=0;
    try{BlendLocalPoses(model,a,target,std::numeric_limits<double>::quiet_NaN());}catch(const Error&){++rejected;}
    try{BlendLocalPoses(model,{},target,.5);}catch(const Error&){++rejected;}
    try{BlendLocalPoses(model,a,target,-.1);}catch(const Error&){++rejected;}
    try{BlendLocalPoses(model,a,target,1.1);}catch(const Error&){++rejected;}
    Require(rejected==4,"Invalid transition input accepted");return 11;
}
}
int main(int argc,char** argv){try{
    Require(argc==2||argc==3,"Usage: ASECM3AnimationBlendInspect model.m3 [bank.m3a]");
    const auto fixtures=Fixtures();auto scene=Scene::Parse(Read(argv[1]));if(argc==3)scene=scene->WithAnimationBank(Read(argv[2]),argv[2]);
    auto previous=scene->EvaluateGroup();bool hasPreviousClip=false;unsigned clips=0,transitions=0,frames=0,authoredBlends=0,worstClip=0;double maximumEndpointError=0,maximumClothDisplacement=0;
    for(uint32_t i=0;i<scene->Model().animations.size();++i){
        if(scene->SequenceGroup(i)<0)continue;
        const auto& sequence=scene->Model().animations[i];const auto target=scene->EvaluateSequence(i,0);++clips;if(sequence.blendMs)++authoredBlends;
        if(hasPreviousClip){
            const auto from=previous.animationLocals;const auto zero=scene->EvaluateSequence(i,0,0,&from,0);
            const auto one=scene->EvaluateSequence(i,0,0,&from,1);
            maximumEndpointError=std::max({maximumEndpointError,Difference(zero.pose,previous.pose),Difference(one.pose,target.pose)});
            Require(maximumEndpointError<1e-5,"Bound source transition endpoint mismatch");
            const auto mid=scene->EvaluateSequence(i,50,0,&from,.5);
            const auto interrupt=scene->EvaluateSequence(i,0,0,&mid.animationLocals,0);
            Require(Difference(mid.pose,interrupt.pose)<1e-5,"Bound transition interruption moved pose");
            ClothSimulation cloth(*scene);auto clothFrame=zero;cloth.Advance(clothFrame,0);
            for(unsigned step=1;step<=12;++step){clothFrame=scene->EvaluateSequence(i,step*100/12,0,&from,double(step)/12);cloth.Advance(clothFrame,1.0/120);++frames;if(cloth.MaximumDisplacement()>maximumClothDisplacement){maximumClothDisplacement=cloth.MaximumDisplacement();worstClip=i;}}
            Require(cloth.Resets()==0,"Continuous transition reset cloth");++transitions;
        }
        previous=scene->EvaluateSequence(i,37);hasPreviousClip=true;
    }
    Require(std::isfinite(maximumClothDisplacement),"Nonfinite transition cloth");
    const auto actions=ResolveActions(scene->Model().animations,argv[1]);
    const int stand=SelectWorldAction(actions,0),walk=SelectWorldAction(actions,5),dance=SelectWorldAction(actions,69);
    unsigned movementTransitions=0;double movementDisplacement=0,pinJumpInstant=0,pinJumpBlended=0;
    for(const auto pair:{std::pair<int,int>{stand,walk},{walk,stand},{stand,dance},{dance,stand}}){
        if(pair.first<0||pair.second<0)continue;
        const auto old=scene->EvaluateSequence(pair.first,37);const auto instant=scene->EvaluateSequence(pair.second,8);
        const auto blended=scene->EvaluateSequence(pair.second,8,0,&old.animationLocals,.08);
        auto region=[](const SceneFrame& f,uint32_t id)->const SceneRegion&{for(const auto& r:f.regions)if(r.division==0&&r.region==id)return r;throw Error("Missing cloth region");};
        auto distance=[](Vec3 a,Vec3 b){return std::sqrt(double(a.x-b.x)*(a.x-b.x)+double(a.y-b.y)*(a.y-b.y)+double(a.z-b.z)*(a.z-b.z));};
        for(const auto& c:scene->Physics().cloths)if(!c.influences.empty())for(size_t v=0;v<c.simulated.size();++v)if(!c.simulated[v]){
            const auto p=region(old,c.simulationRegion).vertices.at(v).position;
            pinJumpInstant=std::max(pinJumpInstant,distance(p,region(instant,c.simulationRegion).vertices.at(v).position));
            pinJumpBlended=std::max(pinJumpBlended,distance(p,region(blended,c.simulationRegion).vertices.at(v).position));
        }
        ClothSimulation cloth(*scene);auto frame=old;cloth.Advance(frame,0);
        for(unsigned step=1;step<=12;++step){frame=scene->EvaluateSequence(pair.second,step*100/12,0,&old.animationLocals,double(step)/12);cloth.Advance(frame,1.0/120);movementDisplacement=std::max(movementDisplacement,cloth.MaximumDisplacement());}
        ++movementTransitions;
    }
    std::cout<<"{\"ok\":true,\"fixtures\":"<<fixtures<<",\"clips\":"<<clips<<",\"authoredBlends\":"<<authoredBlends<<",\"transitions\":"<<transitions<<",\"frames\":"<<frames<<",\"maximumEndpointError\":"<<maximumEndpointError<<",\"maximumClothDisplacement\":"<<maximumClothDisplacement<<",\"worstTargetClip\":"<<worstClip<<",\"movementTransitions\":"<<movementTransitions<<",\"movementClothDisplacement\":"<<movementDisplacement<<",\"maxFirst8msPinMotionInstant\":"<<pinJumpInstant<<",\"maxFirst8msPinMotionBlended\":"<<pinJumpBlended<<",\"scope\":\"Local TRS/shortest rotation/hierarchy, source endpoints and cloth stepping; not visual acceptance. Sequential spell stress can contain large source root displacements.\"}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
