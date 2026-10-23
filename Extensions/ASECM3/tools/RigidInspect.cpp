#include "ASECM3/RigidSimulation.h"
#include "ASECM3/ClothSimulation.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <cmath>
namespace {
std::string Q(const std::string& s){std::string out="\"";for(auto c:s){if(c=='"'||c=='\\')out+='\\';out+=c;}return out+'"';}
std::vector<uint8_t> Read(const std::filesystem::path& path){const auto n=std::filesystem::file_size(path);if(n>128ull*1024*1024)throw ASECM3::Error("Input limit");std::ifstream file(path,std::ios::binary);std::vector<uint8_t> bytes(size_t(n),0);if(!file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(n)))throw ASECM3::Error("Short read");return bytes;}
}
int main(int argc,char** argv){
 if(argc!=2){std::cerr<<"Usage: ASECM3RigidInspect <model.m3|directory>\n";return 2;}
 const std::filesystem::path root=argv[1];std::vector<std::filesystem::path> files;if(std::filesystem::is_directory(root)){for(const auto& f:std::filesystem::recursive_directory_iterator(root))if(f.path().extension()==".m3")files.push_back(f.path());}else files.push_back(root);
 std::cout<<std::setprecision(9)<<"{\"files\":[";bool comma=false;unsigned failed=0;
 for(const auto& path:files){if(comma)std::cout<<',';comma=true;std::cout<<"{\"path\":"<<Q(path.string());try{
  const auto scene=ASECM3::Scene::Parse(Read(path));ASECM3::RigidSimulation sim(*scene),other(*scene);const auto original=scene->EvaluateGroup();auto frame=original,partition=original;double error=0,peakJointError=0;size_t peakContacts=0;
  if(sim.Bodies()){
   for(unsigned i=0;i<240;++i){frame=original;sim.Advance(frame,1.0/120);if(!std::isfinite(sim.MaximumDisplacement()))throw ASECM3::Error("Nonfinite rigid displacement");peakJointError=std::max(peakJointError,sim.MaximumJointError());peakContacts=std::max(peakContacts,sim.GroundContacts());if(peakJointError>.2)throw ASECM3::Error("Rigid joint pivot exceeded .2 source units");}
   for(unsigned i=0;i<60;++i){partition=original;other.Advance(partition,1.0/30);}
   for(size_t i=0;i<frame.pose.world.size();++i)for(unsigned k=0;k<16;++k)error=std::max(error,std::abs(frame.pose.world[i].values[k]-partition.pose.world[i].values[k]));
   if(error>1e-4)throw ASECM3::Error("Rigid fixed-step partition mismatch");
   if(sim.DynamicBodies()&&sim.MaximumDisplacement()<=0)throw ASECM3::Error("Rigid world has no dynamics");
   // Animation-anchored ornament chains need not touch the ground.
   if(sim.DynamicBodies()==sim.Bodies()&&!peakContacts)throw ASECM3::Error("Free ragdoll never produced preview-ground contacts");
   ASECM3::RigidSimulation moving(*scene),resting(*scene);auto previous=original;
   for(auto& world:previous.pose.world)world.At(0,3)-=.08;
   moving.Activate(original,&previous,.008);resting.Activate(original);
   auto moved=original,rest=original;moving.Advance(moved,1.0/120);resting.Advance(rest,1.0/120);
   double velocityEffect=0;for(size_t i=0;i<moved.pose.world.size();++i)velocityEffect=std::max(velocityEffect,moved.pose.world[i].At(0,3)-rest.pose.world[i].At(0,3));
   if(sim.DynamicBodies()==sim.Bodies()&&(!std::isfinite(velocityEffect)||velocityEffect<.01))throw ASECM3::Error("Rigid animation-velocity handoff failed");
   std::cout<<",\"velocityHandoffDisplacement\":"<<velocityEffect;
  }
  std::cout<<",\"ok\":true,\"bodies\":"<<sim.Bodies()<<",\"dynamicBodies\":"<<sim.DynamicBodies()<<",\"joints\":"<<sim.Joints()<<",\"steps\":"<<sim.Steps()<<",\"displacement\":"<<sim.MaximumDisplacement()<<",\"jointPivotError\":"<<sim.MaximumJointError()<<",\"peakJointPivotError\":"<<peakJointError<<",\"peakGroundContacts\":"<<peakContacts<<",\"staticPartitionError\":"<<error;
  if(path.filename()=="alexstrasza_base.m3"){
   auto animated=scene;
   for(const auto& bank:ASECM3::AnimationBankCandidates(path.string()))if(std::filesystem::exists(bank)){animated=scene->WithAnimationBank(Read(bank),bank);break;}
   if(animated->Model().animations.empty())throw ASECM3::Error("Reported-model regression needs the source bank");
   ASECM3::RigidSimulation rigid(*animated);ASECM3::ClothSimulation cloth(*animated);
   if(rigid.DynamicBodies()!=2||rigid.Bodies()!=12)throw ASECM3::Error("Base mesh must retain ten animation anchors and two ornament bodies");
   double anchorError=0,clothMotion=0;
   for(unsigned t=0;t<600;++t){
    auto source=animated->EvaluateSequence(0,t*1000/120),combined=source;
    rigid.Advance(combined,1.0/120);
    for(const auto bone:{1u,2u,3u,6u,7u,10u,11u,13u,44u,55u})for(unsigned k=0;k<16;++k)anchorError=std::max(anchorError,std::abs(combined.pose.world.at(bone).values[k]-source.pose.world.at(bone).values[k]));
    cloth.Advance(combined,1.0/120);clothMotion=std::max(clothMotion,cloth.MaximumDisplacement());
   }
   if(anchorError>1e-5||!std::isfinite(clothMotion))throw ASECM3::Error("Animated base ragdoll/cloth anchor regression");
   std::cout<<",\"baseAnimatedAnchorError\":"<<anchorError<<",\"baseCombinedClothDisplacement\":"<<clothMotion;
  }
  sim.Reset();frame=original;sim.Advance(frame,0);if(sim.MaximumDisplacement()>1e-4)throw ASECM3::Error("Rigid reset failed");
  double resetPoseError=0,resetGeometryError=0;
  for(size_t i=0;i<frame.pose.world.size();++i)for(unsigned k=0;k<16;++k)resetPoseError=std::max(resetPoseError,std::abs(frame.pose.world[i].values[k]-original.pose.world[i].values[k]));
  for(size_t r=0;r<frame.regions.size();++r)for(size_t i=0;i<frame.regions[r].vertices.size();++i){const auto& a=frame.regions[r].vertices[i].position;const auto& b=original.regions[r].vertices[i].position;resetGeometryError=std::max({resetGeometryError,std::abs(double(a.x)-b.x),std::abs(double(a.y)-b.y),std::abs(double(a.z)-b.z)});}
  if(resetPoseError>1e-4||resetGeometryError>1e-4)throw ASECM3::Error("Rigid mass-frame reset moved authored pose or geometry");
  std::cout<<",\"resetPoseError\":"<<resetPoseError<<",\"resetGeometryError\":"<<resetGeometryError;
  std::cout<<'}';
 }catch(const std::exception& e){++failed;std::cout<<",\"ok\":false,\"error\":"<<Q(e.what())<<'}';}}
 std::cout<<"],\"failed\":"<<failed<<",\"scope\":\"Explicit dynamic preview: source shape/joint binding, collision, pose/skinning, stepping/reset. Not source activation or Domino parity.\"}\n";return failed?1:0;
}
