#include "ASECM3/ClothSimulation.h"
#include "ASECM3/ClothBending.h"
#include "ASECM3/ClothTethers.h"
#include "ASECM3/ClothContact.h"
#include "ASECM3/ClothAerodynamics.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <limits>
namespace {
std::vector<uint8_t> Read(const std::filesystem::path& p){const auto n=std::filesystem::file_size(p);if(n>128ull*1024*1024)throw ASECM3::Error("Cloth inspect input exceeds 128 MiB");std::ifstream f(p,std::ios::binary);std::vector<uint8_t> b(size_t(n),0);if(!f.read(reinterpret_cast<char*>(b.data()),std::streamsize(n)))throw ASECM3::Error("Short input");return b;}
std::string Q(const std::string& s){std::string o="\"";for(auto c:s){if(c=='"'||c=='\\')o+='\\';o+=c;}return o+'"';}
double Distance(ASECM3::Vec3 a,ASECM3::Vec3 b){return std::sqrt(double(a.x-b.x)*(a.x-b.x)+double(a.y-b.y)*(a.y-b.y)+double(a.z-b.z)*(a.z-b.z));}
const ASECM3::SceneRegion& Region(const ASECM3::SceneFrame& f,uint32_t n){for(const auto& r:f.regions)if(r.region==n)return r;throw ASECM3::Error("Output region missing");}
unsigned BendingChecks(){
 using namespace ASECM3;unsigned checks=0;
 auto require=[&](bool condition,const char* message){if(!condition)throw Error(message);++checks;};
 const std::array<Vec3,4> flat{{{0,0,0},{1,0,0},{.2f,1,0},{.7f,-1,0}}};
 auto folded=flat;folded[2].z=.4f;folded[3].z=-.1f;double angle;std::array<Vec3,4> gradient;
 require(ClothBendGeometry(folded,angle,&gradient),"Nondegenerate bending fixture rejected");
 for(unsigned v=0;v<4;++v)for(unsigned component=0;component<3;++component){
  auto left=folded,right=folded;auto coordinate=[](Vec3& p,unsigned c)->float&{return c==0?p.x:c==1?p.y:p.z;};
  coordinate(left[v],component)-=.001f;coordinate(right[v],component)+=.001f;double a,b;
  ClothBendGeometry(left,a);ClothBendGeometry(right,b);
  require(std::abs((b-a)/.002-coordinate(gradient[v],component))<.001,"Cloth signed-angle gradient does not match finite differences");
 }
 auto projected=folded;ProjectClothBend(projected,{0,0,1,1},0,.2);double reduced;ClothBendGeometry(projected,reduced);
 require(std::abs(reduced)<std::abs(angle),"Angular projection increased bending error");
 require(Distance(projected[0],folded[0])==0&&Distance(projected[1],folded[1])==0,"Bending moved pinned vertices");
 auto stretched=flat;stretched[2].y=3;stretched[3].y=-2;const auto before=stretched;
 ProjectClothBend(stretched,{1,1,1,1},0,.6);
 double error=0;for(unsigned i=0;i<4;++i)error=std::max(error,Distance(stretched[i],before[i]));
 require(error<1e-7,"Planar stretching incorrectly triggered bending");
 auto scaled=folded;for(auto& p:scaled){p.x*=100;p.y*=100;p.z*=100;}double scaleAngle;ClothBendGeometry(scaled,scaleAngle);
 require(std::abs(scaleAngle-angle)<1e-6,"Bend angle depends on geometric scale");
 const auto restFold=folded;ProjectClothBend(folded,{1,1,1,1},angle,.6);
 error=0;for(unsigned i=0;i<4;++i)error=std::max(error,Distance(folded[i],restFold[i]));
 require(error<1e-7,"Authored rest curvature was flattened");
 auto degenerate=flat;degenerate[1]=degenerate[0];require(!ClothBendGeometry(degenerate,angle),"Degenerate hinge accepted");
 auto pinned=restFold;require(!ProjectClothBend(pinned,{0,0,0,0},0,1),"Fully pinned hinge was projected");
 return checks;
}
unsigned TetherChecks(){
 using namespace ASECM3;const std::vector<Vec3> points{{0,0,0},{0,1,0},{1,1,0},{1,0,0},{10,0,0},{11,0,0}};
 const std::vector<std::array<uint32_t,2>> edges{{0,1},{1,2},{2,3},{4,5}};
 const auto tethers=BuildClothTethers(points,edges,{1,0,0,0,0,0});
 if(tethers.size()!=3||tethers.back().particle!=3||tethers.back().anchor!=0||tethers.back().maximumLength!=3)throw Error("Cloth tether took a shortcut across a mesh hole or used an unanchored component");
 if(!BuildClothTethers(points,edges,{0,0,0,0,0,0}).empty())throw Error("Cloth tether invented an anchor");
 const auto both=BuildClothTethers(points,edges,{1,0,0,1,0,0});
 if(both.size()!=2||both[0].maximumLength!=1||both[1].maximumLength!=1||both[1].anchor!=3)throw Error("Cloth tether nearest anchor mismatch");
 unsigned rejected=0;try{BuildClothTethers(points,edges,{});}catch(const Error&){++rejected;}
 try{BuildClothTethers(points,{{0,6}},{1,0,0,0,0,0});}catch(const Error&){++rejected;}
 try{BuildClothTethers(points,edges,{2,0,0,0,0,0});}catch(const Error&){++rejected;}
 if(rejected!=3)throw Error("Cloth tether accepted invalid input");return 6;
}
unsigned ContactChecks(){
 using namespace ASECM3;unsigned checks=0;auto near=[&](Vec3 a,Vec3 b){if(Distance(a,b)>1e-6)throw Error("Cloth contact normal/tangent mismatch");++checks;};
 near(ConstrainClothContactVelocity({3,4,5},{0,0,1},{}),{3,4,0});
 near(ConstrainClothContactVelocity({3,4,-5},{0,0,1},{}),{3,4,0});
 near(ConstrainClothContactVelocity({3,4,-5},{0,0,1},{8,9,2}),{3,4,2});
 near(ConstrainClothContactVelocity({3,4,-5},{0,0,2},{8,9,2}),{3,4,2});
 unsigned rejected=0;try{ConstrainClothContactVelocity({},{},{});}catch(const Error&){++rejected;}
 try{ConstrainClothContactVelocity({0,0,std::numeric_limits<float>::infinity()},{0,0,1},{});}catch(const Error&){++rejected;}
 if(rejected!=2)throw Error("Invalid cloth contact accepted");return checks+rejected;
}
unsigned AirChecks(){
 using namespace ASECM3;unsigned checks=0;auto require=[&](bool value,const char* message){if(!value)throw Error(message);++checks;};
 const std::array<Vec3,3> points{{{0,0,0},{1,0,0},{0,1,0}}};
 const std::array<Vec3,3> flow{{{0,0,3},{0,0,3},{0,0,3}}};
 const auto drag=ClothAirImpulse(points,flow,{1,1,1},{},1,.2,0,.01);
 require(drag.z<0&&drag.x==0&&drag.y==0,"Normal aerodynamic drag direction mismatch");
 require(Distance(ClothAirImpulse(points,flow,{1,1,1},{},0,.2,.2,.01),{})==0,"Zero air density did not disable aerodynamics");
 require(Distance(ClothAirImpulse(points,flow,{1,1,1},{0,0,3},1,.2,.2,.01),{})==0,"Co-moving air generated an impulse");
 require(Distance(ClothAirImpulse(points,flow,{0,0,0},{},1,.2,.2,.01),{})==0,"Fully pinned triangle generated an impulse");
 require(Distance(ClothAirImpulse({points[0],points[0],points[2]},flow,{1,1,1},{},1,.2,.2,.01),{})==0,"Degenerate air triangle generated an impulse");
 const std::array<Vec3,3> tangent{{{3,0,0},{3,0,0},{3,0,0}}};
 require(Distance(ClothAirImpulse(points,tangent,{1,1,1},{},1,.2,.2,.01),{})<1e-6,"Tangential flow generated normal drag or lift");
 auto angled=flow;for(auto& v:angled)v.x=2;
 const auto lift=ClothAirImpulse(points,angled,{1,1,1},{},1,0,.2,.01);
 const Vec3 after{2+lift.x/3,lift.y/3,3+lift.z/3};
 require(std::abs(Distance(after,{})-std::sqrt(13.0))<1e-6,"Lift changed relative-flow kinetic energy");
 const auto reversed=ClothAirImpulse({points[0],points[2],points[1]},angled,{1,1,1},{},1,.2,.2,.01);
 require(Distance(reversed,ClothAirImpulse(points,angled,{1,1,1},{},1,.2,.2,.01))<1e-6,"Air impulse depends on triangle winding");
 const auto strong=ClothAirImpulse(points,flow,{1,1,1},{},100,100,0,100);
 require(3+strong.z/3>=0&&3+strong.z/3<3,"Strong drag reversed relative flow");
 const auto light=ClothAirImpulse(points,flow,{1,1,1},{},1,.2,0,.01),heavy=ClothAirImpulse(points,flow,{.25,.25,.25},{},1,.2,0,.01);
 require(std::abs(heavy.z*.25)<std::abs(light.z),"Aerodynamic acceleration ignored cloth mass");
 unsigned rejected=0;try{ClothAirImpulse(points,flow,{1,1,1},{},-1,.2,.2,.01);}catch(const Error&){++rejected;}
 try{ClothAirImpulse(points,flow,{1,-1,1},{},1,.2,.2,.01);}catch(const Error&){++rejected;}
 try{ClothAirImpulse(points,flow,{1,1,1},{},1,std::numeric_limits<double>::infinity(),.2,.01);}catch(const Error&){++rejected;}
 require(rejected==3,"Invalid aerodynamic domain accepted");return checks;
}
}
int main(int argc,char** argv){
 if(argc!=2){std::cerr<<"Usage: ASECM3ClothInspect <model.m3|directory>\n";return 2;}
 std::vector<std::filesystem::path> files;const std::filesystem::path p=argv[1];if(std::filesystem::is_directory(p)){for(const auto& f:std::filesystem::recursive_directory_iterator(p))if(f.path().extension()==".m3")files.push_back(f.path());}else files.push_back(p);
 unsigned bendingChecks=0,tetherChecks=0,contactChecks=0,airChecks=0;try{bendingChecks=BendingChecks();tetherChecks=TetherChecks();contactChecks=ContactChecks();airChecks=AirChecks();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
 unsigned failed=0;bool comma=false;std::cout<<std::setprecision(9)<<"{\"bendingChecks\":"<<bendingChecks<<",\"tetherChecks\":"<<tetherChecks<<",\"contactChecks\":"<<contactChecks<<",\"airChecks\":"<<airChecks<<",\"files\":[";
 for(const auto& file:files){if(comma)std::cout<<',';comma=true;std::cout<<"{\"path\":"<<Q(file.string());try{
  auto scene=ASECM3::Scene::Parse(Read(file));if(!scene->Physics().error.empty())throw ASECM3::Error(scene->Physics().error);
  for(const auto& bank:ASECM3::AnimationBankCandidates(file.string()))if(scene->Model().animations.empty()&&std::filesystem::exists(bank)){try{scene=scene->WithAnimationBank(Read(bank),bank);}catch(const ASECM3::Error&){}}
  ASECM3::ClothSimulation sim(*scene),other(*scene);const auto original=scene->EvaluateGroup();auto a=original,b=original;
  if(sim.Cloths()){
   for(unsigned i=0;i<240;++i){a=original;sim.Advance(a,1.0/120);}
   for(unsigned i=0;i<60;++i){b=original;other.Advance(b,1.0/30);}
   double parity=0;for(size_t r=0;r<a.regions.size();++r)for(size_t v=0;v<a.regions[r].vertices.size();++v)parity=std::max(parity,Distance(a.regions[r].vertices[v].position,b.regions[r].vertices[v].position));
   if(parity>1e-5||sim.MaximumDisplacement()<=0||!std::isfinite(sim.MaximumDisplacement()))throw ASECM3::Error("Static cloth timestep partition or finite-motion check failed");
   double mappingError=0;for(const auto& c:scene->Physics().cloths)for(const auto& m:c.influences){const auto& after=Region(a,m.influencedRegion);const auto& before=Region(original,m.influencedRegion);const auto& particles=Region(a,c.simulationRegion);
    const auto& sr=scene->Model().divisions[0].regions[c.simulationRegion];
    for(size_t v=0;v<m.weights.size();++v){ASECM3::Vec3 delta{};unsigned total=0;for(unsigned k=0;k<4;++k){const auto w=(m.weights[v]>>(k*8))&255u;if(!w)continue;total+=w;const auto i=(m.lookups[v]>>(k*16))&65535u;const auto bind=scene->Model().vertices[sr.firstVertex+i].position;ASECM3::Vec3 target{};unsigned tw=0;for(unsigned j=0;j<4;++j){const auto weight=(c.vertexWeights[i]>>(j*8))&255u;if(weight){tw+=weight;const auto bone=(c.vertexBones[i]>>(j*8))&255u;const auto q=ASECM3::TransformPoint(original.pose.skin.at(bone),bind);target.x+=q.x*weight;target.y+=q.y*weight;target.z+=q.z*weight;}}if(tw){target.x/=tw;target.y/=tw;target.z/=tw;}else target=Region(original,c.simulationRegion).vertices[i].position;delta.x+=(particles.vertices[i].position.x-target.x)*w;delta.y+=(particles.vertices[i].position.y-target.y)*w;delta.z+=(particles.vertices[i].position.z-target.z)*w;}
     auto expected=before.vertices[v].position;if(total){expected.x+=delta.x/total;expected.y+=delta.y/total;expected.z+=delta.z/total;}mappingError=std::max(mappingError,Distance(expected,after.vertices[v].position));
    }
   }
   if(mappingError>1e-5)throw ASECM3::Error("PHAC displacement mapping mismatch");
   ASECM3::ClothSimulation moving(*scene),stationary(*scene);auto world=ASECM3::Matrix::Identity();
   moving.SetWorldTransform(world);stationary.SetWorldTransform(world);
   double motionDifference=0,pinError=0;
   for(unsigned i=0;i<120;++i){
    auto movingFrame=original,staticFrame=original;world.At(0,3)=double(i)*.04;moving.SetWorldTransform(world);
    moving.Advance(movingFrame,1.0/120);stationary.Advance(staticFrame,1.0/120);
    for(const auto& c:scene->Physics().cloths){const auto& movingRegion=Region(movingFrame,c.simulationRegion);const auto& staticRegion=Region(staticFrame,c.simulationRegion);
     for(size_t v=0;v<c.simulated.size();++v){const auto d=Distance(movingRegion.vertices[v].position,staticRegion.vertices[v].position);if(!c.simulated[v])pinError=std::max(pinError,d);else motionDifference=std::max(motionDifference,d);}
    }
   }
   if(!std::isfinite(motionDifference)||motionDifference<1e-5||pinError>1e-5)throw ASECM3::Error("World cloth inertia or pin invariance failed");
   const auto resets=moving.Resets();world.At(0,3)+=100;moving.SetWorldTransform(world);if(moving.Resets()!=resets+1)throw ASECM3::Error("World cloth teleport did not reset");
   std::cout<<",\"worldMotionDifference\":"<<motionDifference<<",\"worldMotionPinError\":"<<pinError<<",\"teleportReset\":true";
   ASECM3::ClothSimulation airSimulation(*scene),airPartition(*scene);airSimulation.SetAir(1,{1,0,0});airPartition.SetAir(1,{1,0,0});auto af=original,bf=original;
   for(unsigned i=0;i<120;++i){af=original;airSimulation.Advance(af,1.0/120);}
   for(unsigned i=0;i<30;++i){bf=original;airPartition.Advance(bf,1.0/30);}
   double airError=0;for(size_t r=0;r<af.regions.size();++r)for(size_t v=0;v<af.regions[r].vertices.size();++v)airError=std::max(airError,Distance(af.regions[r].vertices[v].position,bf.regions[r].vertices[v].position));
   if(airError>1e-5||!std::isfinite(airSimulation.MaximumDisplacement()))throw ASECM3::Error("Aerodynamic cloth partition mismatch or nonfinite motion");
   std::cout<<",\"airPartitionMaxError\":"<<airError<<",\"airDisplacement\":"<<airSimulation.MaximumDisplacement();
   const auto displacement=sim.MaximumDisplacement();sim.Reset();a=original;sim.Advance(a,0);if(sim.MaximumDisplacement()!=0)throw ASECM3::Error("Cloth reset retained deformation");
   std::cout<<",\"staticPartitionMaxError\":"<<parity<<",\"mappingMaxError\":"<<mappingError<<",\"staticDisplacement\":"<<displacement;
   if(!scene->Model().animations.empty()){for(unsigned i=0;i<240;++i){a=scene->EvaluateSequence(0,i*1000/120);sim.Advance(a,1.0/120);}if(!std::isfinite(sim.MaximumDisplacement()))throw ASECM3::Error("Animated cloth displacement is nonfinite");}
   a=original;sim.Advance(a,1);if(sim.MaximumDisplacement()!=0)throw ASECM3::Error("Discontinuous clock did not reset cloth");
  }
  std::cout<<",\"ok\":true,\"cloths\":"<<sim.Cloths()<<",\"particles\":"<<sim.Particles()<<",\"mappedVertices\":"<<sim.MappedVertices()<<",\"bendingConstraints\":"<<sim.BendingConstraints()<<",\"tethers\":"<<sim.Tethers()<<",\"frequencyHz\":"<<sim.FrequencyHz()<<",\"projectionIterations\":"<<sim.ProjectionIterations()<<",\"steps\":"<<sim.Steps()<<'}';
 }catch(const std::exception& e){++failed;std::cout<<",\"ok\":false,\"error\":"<<Q(e.what())<<'}';}}
 std::cout<<"],\"failed\":"<<failed<<",\"scope\":\"Source cloth binding, preview PBD stepping/mapping/reset only; not Domino or rigid-body parity\"}\n";return failed?1:0;
}
