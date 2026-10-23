#include "ASECM3/Pose.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
static std::string Quote(const std::string& value) {
    std::string out="\"";
    for (unsigned char c:value) { if (c=='"'||c=='\\') out+='\\'; if (c<32) out+=' '; else out+=char(c); }
    return out+'"';
}
static void Matrices(const std::vector<ASECM3::Matrix>& matrices) {
    std::cout<<'[';bool comma=false;
    for (const auto& m:matrices) { if (comma) std::cout<<',';comma=true;std::cout<<'[';
        for (int i=0;i<16;++i) {if(i)std::cout<<',';std::cout<<m.values[i];}std::cout<<']'; }
    std::cout<<']';
}
static void Points(const std::vector<ASECM3::Vec3>& points) {
    std::cout<<'[';bool comma=false;
    for (const auto& p:points) {if(comma)std::cout<<',';comma=true;std::cout<<'['<<p.x<<','<<p.y<<','<<p.z<<']';}
    std::cout<<']';
}
int main(int argc,char** argv) {
    if (argc!=4) {std::cerr<<"Usage: ASECM3PoseInspect <file> <group|-1 for defaults> <source-time-ms>\n";return 2;}
    try {
        const auto length=std::filesystem::file_size(argv[1]);if(length>512ull*1024*1024)throw ASECM3::Error("Input exceeds size limit");
        std::ifstream file(argv[1],std::ios::binary);std::vector<uint8_t> bytes(static_cast<size_t>(length));
        if(!file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(length)))throw ASECM3::Error("Could not read model");
        const auto doc=ASECM3::Document::Parse(std::move(bytes));const auto model=ASECM3::DecodeModel(doc);
        const auto bank=ASECM3::DecodeAnimationBank(doc);
        const auto pose=ASECM3::EvaluatePoseAtSourceTime(model,bank,std::stoi(argv[2]),std::stoi(argv[3]));
        const auto regions=ASECM3::SkinModel(model,pose);
        std::cout<<std::setprecision(17)<<"{\"ok\":true,\"defaultBindError\":"<<pose.maximumDefaultBindError<<",\"world\":";
        Matrices(pose.world);std::cout<<",\"skin\":";Matrices(pose.skin);std::cout<<",\"normal\":";Matrices(pose.normal);
        std::cout<<",\"regions\":[";bool comma=false;
        for(const auto& region:regions) {if(comma)std::cout<<',';comma=true;
            std::cout<<"{\"division\":"<<region.division<<",\"region\":"<<region.region<<",\"positions\":";
            Points(region.positions);std::cout<<",\"normals\":";Points(region.normals);std::cout<<",\"tangents\":";Points(region.tangents);std::cout<<",\"tangentSigns\":[";
            for(size_t i=0;i<region.tangentSigns.size();++i){if(i)std::cout<<',';std::cout<<region.tangentSigns[i];}std::cout<<"]}";}
        std::cout<<"]}\n";return 0;
    } catch(const std::exception& e) {std::cout<<"{\"ok\":false,\"error\":"<<Quote(e.what())<<"}\n";return 1;}
}
