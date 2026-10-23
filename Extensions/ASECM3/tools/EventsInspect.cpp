#include "ASECM3/Scene.h"
#include "ASECM3/Events.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
namespace {
std::string Q(const std::string& s){std::string out="\"";const char* hex="0123456789abcdef";for(unsigned char c:s){if(c=='"'||c=='\\'){out+='\\';out+=char(c);}else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}else out+=char(c);}return out+'"';}
std::vector<uint8_t> Read(const std::filesystem::path& path){const auto n=std::filesystem::file_size(path);if(n>128ull*1024*1024)throw ASECM3::Error("Input limit");std::ifstream f(path,std::ios::binary);std::vector<uint8_t> b(size_t(n),0);if(!f.read(reinterpret_cast<char*>(b.data()),std::streamsize(n)))throw ASECM3::Error("Short input");return b;}
void CheckClock(){
 ASECM3::AnimationBank bank;ASECM3::TransformCollection c;ASECM3::EventTrack track;track.id=7;
 for(auto time:{0,5,10}){ASECM3::AnimationEvent e;e.time=time;e.name="event";track.events.push_back(e);}c.events[7]=track;bank.collections.push_back(c);bank.groups.push_back({"clip",{0}});ASECM3::Animation clip{"clip",0,10,0};
 const auto all=ASECM3::EventsBetween(bank,0,clip,0,20,true),a=ASECM3::EventsBetween(bank,0,clip,0,5,true),b=ASECM3::EventsBetween(bank,0,clip,5,20);
 if(all.size()!=7||a.size()!=2||b.size()!=5||all[2].cycle!=0||all[3].cycle!=1)throw ASECM3::Error("Event loop-boundary dispatch failed");
 clip.flags=1;const auto once=ASECM3::EventsBetween(bank,0,clip,0,20,true);if(once.size()!=3)throw ASECM3::Error("Nonloop event dispatch repeated");
 ASECM3::UnsignedTrack values;values.times={0,5,10};values.values={0xffffffffu,16777217u,0x80000000u};for(unsigned i=0;i<3;++i)if(ASECM3::SampleUnsigned(values,values.times[i],0)!=values.values[i])throw ASECM3::Error("Unsigned state track lost integer precision");
}
}
int main(int argc,char** argv){
 if(argc!=2){std::cerr<<"Usage: ASECM3EventsInspect <file|directory>\n";return 2;}
 try{CheckClock();}catch(const std::exception& e){std::cerr<<e.what();return 1;}
 const std::filesystem::path root=argv[1];std::vector<std::filesystem::path> files;if(std::filesystem::is_directory(root)){for(const auto& f:std::filesystem::recursive_directory_iterator(root))if(f.path().extension()==".m3"||f.path().extension()==".m3a")files.push_back(f.path());}else files.push_back(root);std::sort(files.begin(),files.end());
 std::cout<<"{\"clockChecks\":true,\"files\":[";bool comma=false;unsigned failed=0;
 for(const auto& path:files){if(comma)std::cout<<',';comma=true;std::cout<<"{\"path\":"<<Q(path.string());try{
  const auto scene=ASECM3::Scene::Parse(Read(path));const auto& bank=scene->Animation();std::cout<<",\"collections\":[";bool cc=false;
  for(const auto& c:bank.collections){if(cc)std::cout<<',';cc=true;std::cout<<"{\"name\":"<<Q(c.name)<<",\"events\":[";bool ec=false;for(const auto& t:c.events)for(const auto& e:t.second.events){if(ec)std::cout<<',';ec=true;std::cout<<"{\"track\":"<<t.first<<",\"time\":"<<e.time<<",\"name\":"<<Q(e.name)<<",\"id\":"<<e.id<<",\"bone\":"<<e.bone<<",\"flags\":"<<e.flags<<",\"payload\":"<<Q(e.payload)<<",\"param0\":"<<e.param0<<",\"param1\":"<<e.param1<<'}';}
   std::cout<<"],\"integers\":[";ec=false;for(const auto& t:c.unsignedValues){if(ec)std::cout<<',';ec=true;std::cout<<"{\"id\":"<<t.first<<",\"times\":[";for(size_t i=0;i<t.second.times.size();++i){if(i)std::cout<<',';std::cout<<t.second.times[i];}std::cout<<"],\"values\":[";for(size_t i=0;i<t.second.values.size();++i){if(i)std::cout<<',';std::cout<<t.second.values[i];}std::cout<<"]}";}std::cout<<"]}";
  }
  std::cout<<"],\"partitionChecks\":true";
  for(uint32_t seq=0;seq<scene->Model().animations.size();++seq){const auto group=scene->SequenceGroup(seq);if(group<0)continue;const auto& clip=scene->Model().animations[seq];const uint64_t duration=uint64_t(clip.end)-clip.start,end=duration*2+1;const auto full=ASECM3::EventsBetween(bank,group,clip,0,end,true);std::vector<ASECM3::EventOccurrence> split;for(uint64_t time=0;time<end;){const auto next=std::min(end,time+137);const auto part=ASECM3::EventsBetween(bank,group,clip,time,next,time==0);split.insert(split.end(),part.begin(),part.end());time=next;}if(full.size()!=split.size())throw ASECM3::Error("Source event partition count mismatch");for(size_t i=0;i<full.size();++i)if(full[i].elapsed!=split[i].elapsed||full[i].cycle!=split[i].cycle||full[i].event.name!=split[i].event.name)throw ASECM3::Error("Source event partition ordering mismatch");}
  std::cout<<",\"ok\":true}";
 }catch(const std::exception& e){++failed;std::cout<<",\"ok\":false,\"error\":"<<Q(e.what())<<'}';}}
 std::cout<<"],\"failed\":"<<failed<<",\"scope\":\"Event/integer source decode and timeline partitioning; no general actor/state-enum interpretation\"}\n";return failed?1:0;
}
