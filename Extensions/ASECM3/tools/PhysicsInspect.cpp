#include "ASECM3/Physics.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cctype>
namespace {
std::string Q(const std::string& s){std::string o="\"";for(unsigned char c:s){if(c=='"'||c=='\\')o+='\\';o+=c<32?' ':char(c);}return o+'"';}
void Matrix(const ASECM3::Matrix& m){std::cout<<'[';for(unsigned k=0;k<16;++k){if(k)std::cout<<',';std::cout<<m.values[k];}std::cout<<']';}
}
int main(int argc,char** argv){
 if(argc!=2){std::cerr<<"Usage: ASECM3PhysicsInspect <model.m3|directory>\n";return 2;}
 std::vector<std::filesystem::path> paths;const std::filesystem::path root=argv[1];
 if(std::filesystem::is_directory(root)){for(const auto& file:std::filesystem::recursive_directory_iterator(root))if(file.is_regular_file()){auto ext=file.path().extension().string();for(auto& c:ext)c=char(std::tolower(static_cast<unsigned char>(c)));if(ext==".m3"||ext==".m3a")paths.push_back(file.path());}}
 else paths.push_back(root);
 std::sort(paths.begin(),paths.end());unsigned failed=0;std::cout<<std::setprecision(9)<<"{\"files\":[";bool comma=false;
 for(const auto& path:paths){if(comma)std::cout<<',';comma=true;std::cout<<"{\"path\":"<<Q(path.string());
  try{
   const auto n=std::filesystem::file_size(path);if(n>512ull*1024*1024)throw ASECM3::Error("Input limit");std::ifstream file(path,std::ios::binary);std::vector<uint8_t> bytes(static_cast<size_t>(n));if(!file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(n)))throw ASECM3::Error("Short input");
   const auto document=ASECM3::Document::Parse(std::move(bytes));const auto model=ASECM3::DecodeModel(document);const auto data=ASECM3::DecodePhysics(document,model);
   std::cout<<",\"ok\":true,\"clothCount\":"<<data.clothCount<<",\"constraintCount\":"<<data.constraintCount<<",\"bodies\":[";bool sep=false;
   for(const auto& b:data.bodies){if(sep)std::cout<<',';sep=true;std::cout<<"{\"version\":"<<b.version<<",\"bone\":"<<b.bone<<",\"boneName\":"<<Q(model.skeleton.at(b.bone).name)<<",\"simulationType\":"<<b.simulationType<<",\"flags\":"<<b.flags<<",\"mass\":"<<b.mass<<",\"friction\":"<<b.friction<<",\"bounce\":"<<b.bounce<<",\"linearDamping\":"<<b.linearDamping<<",\"angularDamping\":"<<b.angularDamping<<",\"gravityFactor\":"<<b.gravityFactor<<",\"localForces\":"<<b.localForces<<",\"worldForces\":"<<b.worldForces<<",\"shapes\":[";bool sc=false;
    for(const auto& s:b.shapes){if(sc)std::cout<<',';sc=true;std::cout<<"{\"version\":"<<s.version<<",\"kind\":"<<s.kind<<",\"size\":["<<s.size[0]<<','<<s.size[1]<<','<<s.size[2]<<"],\"vertices\":"<<s.vertices.size()<<",\"indices\":"<<s.faces.size()<<",\"matrix\":";Matrix(s.local);std::cout<<'}';}
    std::cout<<"]}";
   }
   std::cout<<"],\"joints\":[";sep=false;
   for(const auto& j:data.joints){if(sep)std::cout<<',';sep=true;std::cout<<"{\"kind\":"<<j.kind<<",\"endpoint1\":"<<j.endpoint1<<",\"endpoint2\":"<<j.endpoint2<<",\"limit\":"<<(j.limit?"true":"false")<<",\"minimum\":"<<j.minimum<<",\"maximum\":"<<j.maximum<<",\"angle\":"<<j.angle<<",\"frame1\":";Matrix(j.frame1);std::cout<<",\"frame2\":";Matrix(j.frame2);std::cout<<'}';}
   std::cout<<"],\"cloths\":[";sep=false;
   for(const auto& c:data.cloths){if(sep)std::cout<<',';sep=true;std::cout<<"{\"version\":"<<c.version<<",\"simulationRegion\":"<<c.simulationRegion<<",\"skinBones\":"<<c.skinBones.size()<<",\"vertices\":"<<c.simulated.size()<<",\"simulatedVertices\":"<<std::count_if(c.simulated.begin(),c.simulated.end(),[](auto v){return v!=0;})<<",\"constraints\":"<<c.constraints.size()<<",\"influences\":[";bool ic=false;for(const auto& p:c.influences){if(ic)std::cout<<',';ic=true;std::cout<<"{\"region\":"<<p.influencedRegion<<",\"simulationRegion\":"<<p.simulationRegion<<",\"vertices\":"<<p.lookups.size()<<'}';}std::cout<<"],\"coefficients\":{";ic=false;for(const auto& p:c.coefficients){if(ic)std::cout<<',';ic=true;std::cout<<Q(p.first)<<':'<<p.second;}std::cout<<"},\"hasLocalWind\":"<<(c.hasLocalWind?"true":"false")<<",\"localWind\":["<<c.localWind.x<<','<<c.localWind.y<<','<<c.localWind.z<<"]}";}
   std::cout<<"],\"payloads\":[";sep=false;size_t retained=0;
   for(const auto& p:data.payloads){if(sep)std::cout<<',';sep=true;retained+=p.bytes.size();std::cout<<"{\"section\":"<<p.section<<",\"tag\":"<<Q(p.tag)<<",\"version\":"<<p.version<<",\"count\":"<<p.count<<",\"bytes\":"<<p.bytes.size()<<'}';}
   std::cout<<"],\"retainedBytes\":"<<retained<<'}';
  }catch(const std::exception& e){++failed;std::cout<<",\"ok\":false,\"error\":"<<Q(e.what())<<'}';}
 }
 std::cout<<"],\"failed\":"<<failed<<",\"scope\":\"Physics source decoding only; no solver or visibility claim\"}\n";return failed?1:0;
}
