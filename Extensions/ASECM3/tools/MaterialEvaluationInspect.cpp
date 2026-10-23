#include "ASECM3/MaterialEvaluation.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
static std::string Quote(const std::string& s){std::string out="\"";for(unsigned char c:s){if(c=='\\'||c=='\"')out+='\\';if(c<32)out+=' ';else out+=c;}return out+'\"';}
int main(int argc,char** argv){
 if(argc!=4){std::cerr<<"Usage: ASECM3MaterialEvaluationInspect <file> <group|-1> <source-ms>\n";return 2;}
 try{
  const auto size=std::filesystem::file_size(argv[1]);if(size>128ull*1024*1024)throw ASECM3::Error("Material input limit");
  std::ifstream file(argv[1],std::ios::binary);std::vector<uint8_t> bytes(static_cast<size_t>(size));if(!file.read(reinterpret_cast<char*>(bytes.data()),size))throw ASECM3::Error("Short material read");
  const auto doc=ASECM3::Document::Parse(std::move(bytes));const auto model=ASECM3::DecodeModel(doc);
  const auto catalog=ASECM3::DecodeMaterials(doc,model);const auto bank=ASECM3::DecodeAnimationBank(doc);
  const ASECM3::MaterialEvaluator values(bank,std::stoi(argv[2]),std::stoi(argv[3]));
  const auto passes=ASECM3::EvaluateMaterialPasses(catalog,values);
  std::cout<<std::setprecision(9)<<"{\"ok\":true,\"passes\":[";bool comma=false;
  for(const auto& pass:passes){
   if(comma)std::cout<<',';comma=true;
   std::cout<<"{\"batch\":"<<pass.batch<<",\"material\":"<<pass.material<<",\"opacity\":"<<pass.opacity<<",\"blend\":"<<pass.blend<<",\"priority\":"<<pass.priority<<",\"composite\":"<<(pass.composite?"true":"false")<<",\"deferred\":"<<(pass.deferred?"true":"false")<<'}';
  }
  std::cout<<"],\"packets\":[";comma=false;
  for(const auto& packet:ASECM3::PackMaterialPasses(passes)){
   if(comma)std::cout<<',';comma=true;
   std::cout<<"{\"first\":"<<packet.first<<",\"count\":"<<packet.count<<",\"batch\":"<<packet.batch<<",\"priority\":"<<packet.priority<<",\"deferred\":"<<(packet.deferred?"true":"false")<<'}';
  }
  std::cout<<"],\"values\":[";comma=false;
  auto output=[&](const ASECM3::MaterialRecord& record,const std::string& path){
   for(const auto& p:record.properties){
    const std::string suffix=".header.id";if(p.name.size()<suffix.size()||p.name.substr(p.name.size()-suffix.size())!=suffix)continue;
    const auto field=p.name.substr(0,p.name.size()-suffix.size());uint32_t type=0,components=0;
    const auto* scalar=record.Find(field+".default");
    if(scalar&&scalar->kind=="float"){type=5;components=1;}
    else if(record.Find(field+".default.r")){type=4;components=4;}
    else if(record.Find(field+".default.x")){type=record.Find(field+".default.w")?3:record.Find(field+".default.z")?2:1;components=type==3?4:type==2?3:2;}
    if(!type)continue;
    const auto sample=values.Sample(record,field,type,{});
    if(comma)std::cout<<',';comma=true;std::cout<<"{\"path\":"<<Quote(path+"."+field)<<",\"type\":"<<type<<",\"value\":[";
    for(uint32_t i=0;i<components;++i){if(i)std::cout<<',';std::cout<<sample[i];}std::cout<<"]}";
   }
  };
  for(size_t i=0;i<catalog.materials.size();++i){const auto& material=catalog.materials[i];const auto prefix="m"+std::to_string(i);output(material.record,prefix);
   for(size_t j=0;j<material.layers.size();++j)output(material.layers[j].record,prefix+".l"+std::to_string(j));
   for(size_t j=0;j<material.composite.size();++j)output(material.composite[j].record,prefix+".c"+std::to_string(j));
  }
  std::cout<<"],\"layerControls\":[";comma=false;
  auto controls=[&](const ASECM3::Material& material,const std::string& prefix){for(size_t j=0;j<material.layers.size();++j){const auto& layer=material.layers[j];if(comma)std::cout<<',';comma=true;std::cout<<"{\"path\":"<<Quote(prefix+".l"+std::to_string(j))<<",\"role\":"<<Quote(layer.role)<<",\"clamp\":"<<(ASECM3::LayerClampsColor(layer.record)?"true":"false")<<'}';}};
  for(size_t i=0;i<catalog.materials.size();++i){const auto& material=catalog.materials[i];const auto prefix="m"+std::to_string(i);controls(material,prefix);if(material.bufferSurface)controls(*material.bufferSurface,prefix+".buffer");}
  std::cout<<"]}\n";return 0;
 }catch(const std::exception& e){std::cout<<"{\"ok\":false,\"error\":"<<Quote(e.what())<<"}\n";return 1;}
}
