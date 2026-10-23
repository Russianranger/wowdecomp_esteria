#include "ASECM3/ParticleEmitter.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
using namespace ASECM3;
namespace {
std::vector<uint8_t> Read(const char* path){const auto size=std::filesystem::file_size(path);if(size>128ull*1024*1024)throw Error("Birth inspection input exceeds 128 MiB");std::ifstream file(path,std::ios::binary);std::vector<uint8_t> bytes(static_cast<size_t>(size));if(!file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(size)))throw Error("Short birth inspection input");return bytes;}
Matrix World(double ms){const double t=ms*.001;return Compose({float(t*3),float(std::sin(t)),float(t*.25),0},{0,0,float(std::sin(t*.15)),float(std::cos(t*.15))},{float(1+t*.1),float(1+t*.1),float(1+t*.1),0});}
bool Same(const ParticleState& a,const ParticleState& b){return a.serial==b.serial&&a.birthMs==b.birthMs&&a.age==b.age&&a.lifetime==b.lifetime&&a.position.x==b.position.x&&a.position.y==b.position.y&&a.position.z==b.position.z&&a.velocity.x==b.velocity.x&&a.velocity.y==b.velocity.y&&a.velocity.z==b.velocity.z&&a.visual.size==b.visual.size&&a.visual.rotation==b.visual.rotation&&a.visual.color==b.visual.color&&a.visual.u0==b.visual.u0&&a.visual.v0==b.visual.v0&&a.visual.u1==b.visual.u1&&a.visual.v1==b.visual.v1;}
}
int main(int argc,char** argv){
 if(argc!=3){std::cerr<<"Usage: ASECM3ParticleBirthInspect <model.m3> <effectbank.m3a>\n";return 2;}
 try{
  const auto scene=Scene::Parse(Read(argv[1]))->WithEffectAnimationBank(Read(argv[2]),argv[2]);
  if(scene->Model().animations.empty())throw Error("Birth inspection requires an authored body clip");
  size_t cases=0,samples=0,different=0,rollback=0;
  double maxEndPoseError=0;
  for(uint32_t sequence=0;sequence<scene->EffectSequences().size();++sequence)for(const auto& link:scene->EffectBinding().particles){
   const auto& definition=scene->Effects().particles.at(link.meshEmitter);
   for(uint64_t end:{uint64_t(500),uint64_t(1000),uint64_t(2000),uint64_t(3333),uint64_t(13333)}){
    ParticleEmitter whole(scene,link.meshEmitter,sequence),chunked(scene,link.meshEmitter,sequence),fixed(scene,link.meshEmitter,sequence);
    ParticleBirthSampler source=[&](double time,uint32_t& random,ParticleBirthSample& out){
     if(time<0||time>double(end))throw Error("Birth source requested outside inspected interval");
     const auto pose=scene->EvaluateSequence(0,uint64_t(std::floor(time)));
     MeshEmissionSurface surface(definition,pose);out.modelToWorld=World(time);return surface.Sample(random,out.point);
    };
    size_t callbackCount=0;
    ParticleBirthSampler tracked=[&](double time,uint32_t& random,ParticleBirthSample& out){
     ++callbackCount;const auto before=random;const bool found=source(time,random,out);
     if(found){auto controlRandom=before;EmissionPoint control;const MeshEmissionSurface endSurface(definition,scene->EvaluateSequence(0,end));if(endSurface.Sample(controlRandom,control)){const double dx=double(out.point.position.x)-control.position.x,dy=double(out.point.position.y)-control.position.y,dz=double(out.point.position.z)-control.position.z;maxEndPoseError=std::max(maxEndPoseError,std::sqrt(dx*dx+dy*dy+dz*dz));}}
     return found;
    };
    // A rejected callback must not commit clock, RNG, counters or particles.
    bool rejected=false;try{whole.AdvanceSampled(end,[](double,uint32_t&,ParticleBirthSample&)->bool{throw Error("Expected birth callback rejection");});}catch(const Error&){rejected=true;}
    if(!rejected||whole.ElapsedMs()||whole.Spawned()||whole.Dropped()||whole.NoSurface()||whole.Alive())throw Error("Birth callback failure committed emitter state");++rollback;
    whole.AdvanceSampled(end,tracked);
    for(uint64_t time=0;;time=std::min(end,time+17)){chunked.AdvanceSampled(time,source);if(time==end)break;}
    fixed.Advance(end,scene->EvaluateSequence(0,end),World(double(end)));
    const auto a=whole.Snapshot(World(double(end))),b=chunked.Snapshot(World(double(end))),c=fixed.Snapshot(World(double(end)));
    if(a.size()!=b.size()||whole.Spawned()!=chunked.Spawned()||whole.Dropped()!=chunked.Dropped()||whole.NoSurface()!=chunked.NoSurface())throw Error("Birth sampling counters depend on render partition");
    for(size_t i=0;i<a.size();++i)if(!Same(a[i],b[i]))throw Error("Birth pose or transform depends on render partition");
    for(size_t i=0;i<std::min(a.size(),c.size());++i)if(!Same(a[i],c[i]))++different;
    samples+=callbackCount;++cases;
   }
  }
  if(!cases||!samples||!different)throw Error("Moving birth inspection did not exercise time-dependent inputs");
  std::cout<<std::setprecision(12)<<"{\"ok\":true,\"cases\":"<<cases<<",\"acceptedBirthSamples\":"<<samples<<",\"differentFromEndFrame\":"<<different<<",\"maximumSourcePoseDifference\":"<<maxEndPoseError<<",\"rollbackChecks\":"<<rollback<<",\"partitionMatched\":true,\"scope\":\"Source clip at integer birth ms with explicit moving actor fixture; not native physics history or source-engine parity\"}\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
