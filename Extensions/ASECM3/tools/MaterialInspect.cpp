#include "ASECM3/Material.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
static std::string Quote(const std::string& text) {
    std::string out="\""; const char* hex="0123456789abcdef";
    for (unsigned char c:text) {
        if (c=='"' || c=='\\') { out+='\\'; out+=char(c); }
        else if (c<32) { out+="\\u00"; out+=hex[c>>4]; out+=hex[c&15]; }
        else out+=char(c);
    }
    return out+'"';
}

static void Hex(const std::vector<uint8_t>& bytes) {
    static const char* digits="0123456789abcdef";
    std::cout<<'"'; for (auto v:bytes) std::cout<<digits[v>>4]<<digits[v&15]; std::cout<<'"';
}
static void Record(const ASECM3::MaterialRecord& record) {
    std::cout<<"{\"tag\":"<<Quote(record.tag)<<",\"version\":"<<record.version<<",\"bytes\":"; Hex(record.bytes);
    std::cout<<",\"properties\":["; bool comma=false;
    for (const auto& property:record.properties) {
        if (comma) std::cout<<','; comma=true;
        std::cout<<"{\"name\":"<<Quote(property.name)<<",\"kind\":"<<Quote(property.kind)<<",\"value\":";
        std::visit([](auto value){std::cout<<value;},property.value); std::cout<<'}';
    }
    std::cout<<"],\"references\":["; comma=false;
    for (const auto& reference:record.references) {
        if (comma) std::cout<<','; comma=true;
        std::cout<<"{\"name\":"<<Quote(reference.name)<<",\"tag\":"<<Quote(reference.tag)
            <<",\"count\":"<<reference.value.count<<",\"section\":"<<reference.value.section<<",\"flags\":"<<reference.value.flags<<'}';
    }
    std::cout<<"]}";
}
int main(int argc,char** argv) {
    if (argc!=2) return 2;
    try {
        const std::filesystem::path file=argv[1]; const auto size=std::filesystem::file_size(file);
        if (size>512ull*1024*1024) throw ASECM3::Error("Input size limit");
        std::ifstream input(file,std::ios::binary); std::vector<uint8_t> bytes(static_cast<size_t>(size));
        if (!input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(size))) throw ASECM3::Error("Read failed");
        const auto document=ASECM3::Document::Parse(std::move(bytes));
        const auto model=ASECM3::DecodeModel(document); const auto catalog=ASECM3::DecodeMaterials(document,model);
        std::cout<<std::setprecision(9)<<"{\"ok\":true,\"materials\":["; bool comma=false;
        for (const auto& material:catalog.materials) {
            if (comma) std::cout<<','; comma=true;
            std::cout<<"{\"name\":"<<Quote(material.name)<<",\"type\":"<<material.type<<",\"sourceIndex\":"<<material.sourceIndex<<",\"record\":";
            Record(material.record); std::cout<<",\"layers\":["; bool inner=false;
            for (const auto& layer:material.layers) {
                if (inner) std::cout<<','; inner=true;
                std::cout<<"{\"role\":"<<Quote(layer.role)<<",\"image\":"<<Quote(layer.image)<<",\"sourceSection\":"<<layer.sourceSection
                    <<",\"sourceIndex\":"<<layer.sourceIndex<<",\"record\":"; Record(layer.record); std::cout<<'}';
            }
            std::cout<<"],\"composite\":["; inner=false;
            for (const auto& section:material.composite) {
                if (inner) std::cout<<','; inner=true;
                std::cout<<"{\"materialReference\":"<<section.materialReference<<",\"record\":"; Record(section.record); std::cout<<'}';
            }
            std::cout<<"],\"bufferTextures\":["; inner=false;
            for (const auto& texture:material.bufferTextures) { if (inner) std::cout<<','; inner=true; std::cout<<Quote(texture); }
            std::cout<<"],\"bufferPayloads\":["; inner=false;
            for (const auto& payload:material.bufferPayloads) {
                if (inner) std::cout<<','; inner=true;
                std::cout<<"{\"role\":"<<Quote(payload.role)<<",\"tag\":"<<Quote(payload.tag)<<",\"count\":"<<payload.count
                    <<",\"version\":"<<payload.version<<",\"bytes\":"; Hex(payload.bytes); std::cout<<'}';
            }
            std::cout<<"],\"bufferDecodeError\":"<<Quote(material.bufferDecodeError)<<",\"bufferGroups\":["; inner=false;
            for(const auto& group:material.bufferParameters.groups) {
                if(inner)std::cout<<',';inner=true;
                std::cout<<"{\"id\":"<<group.id<<",\"sourceOffset\":"<<group.sourceOffset<<",\"parameters\":[";bool fieldComma=false;
                for(const auto& field:group.parameters) {
                    if(fieldComma)std::cout<<',';fieldComma=true;
                    std::cout<<"{\"id\":"<<field.id<<",\"byteWidth\":"<<field.byteWidth<<",\"flags\":"<<unsigned(field.flags)
                        <<",\"sourceOffset\":"<<field.sourceOffset<<",\"bytes\":";Hex(field.bytes);std::cout<<'}';
                }
                std::cout<<"]}";
            }
            std::cout<<"],\"bufferSurfaceError\":"<<Quote(material.bufferSurfaceError)<<",\"bufferSurface\":";
            if(!material.bufferSurface)std::cout<<"null";
            else {
                std::cout<<"{\"record\":";Record(material.bufferSurface->record);std::cout<<",\"layers\":[";bool comma=false;
                for(const auto& layer:material.bufferSurface->layers){if(comma)std::cout<<',';comma=true;std::cout<<"{\"role\":"<<Quote(layer.role)<<",\"image\":"<<Quote(layer.image)<<",\"record\":";Record(layer.record);std::cout<<'}';}
                std::cout<<"]}";
            }
            std::cout<<'}';
        }
        std::cout<<"],\"batches\":["; comma=false;
        for (const auto& batch:catalog.batches) {
            if (comma) std::cout<<','; comma=true;
            std::cout<<"{\"division\":"<<batch.division<<",\"index\":"<<batch.index<<",\"region\":"<<batch.region<<",\"material\":"<<batch.material
                <<",\"flags\":"<<batch.flags<<",\"priorityPlane\":"<<batch.priorityPlane<<",\"boundsIndex\":"<<batch.boundsIndex
                <<",\"colorIndex\":"<<batch.colorIndex<<",\"bone\":"<<batch.bone<<'}';
        }
        std::cout<<"]}\n";
        return 0;
    } catch (const std::exception& e) { std::cout<<"{\"ok\":false,\"error\":"<<Quote(e.what())<<"}\n"; return 1; }
}
