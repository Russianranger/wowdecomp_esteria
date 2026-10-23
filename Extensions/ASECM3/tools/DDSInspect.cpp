#include "ASECM3/DDS.h"
#include <filesystem>
#include <fstream>
#include <iostream>
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        const auto size=std::filesystem::file_size(argv[1]);if(size>512ull*1024*1024)throw ASECM3::Error("DDS size limit exceeded");
        std::ifstream file(argv[1],std::ios::binary);std::vector<uint8_t> bytes(static_cast<size_t>(size));
        if(!file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(size)))throw ASECM3::Error("DDS read failed");
        const auto image=ASECM3::DDSImage::Parse(std::move(bytes));
        std::cout<<"{\"ok\":true,\"width\":"<<image.width<<",\"height\":"<<image.height<<",\"faces\":"<<image.faces<<",\"levels\":"<<image.levels
            <<",\"format\":\""<<ASECM3::DDSFormatName(image.format)<<"\",\"mips\":[";
        bool comma=false;for(const auto& mip:image.mips) {if(comma)std::cout<<',';comma=true;
            image.Pixels(mip);std::cout<<"{\"face\":"<<mip.face<<",\"level\":"<<mip.level<<",\"width\":"<<mip.width<<",\"height\":"<<mip.height
                <<",\"offset\":"<<mip.offset<<",\"size\":"<<mip.size<<",\"rowBytes\":"<<mip.rowBytes<<'}';}
        std::cout<<"]}\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
