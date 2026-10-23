#include "ASECM3/Effects.h"
#include <unordered_map>
#include <cstring>
#include <cmath>
namespace ASECM3 {
namespace {
constexpr size_t Budget=64u*1024*1024;
void Charge(size_t bytes,size_t& owned){if(bytes>Budget-owned)throw Error("M3 decoded effects exceed 64 MiB");owned+=bytes;}
MaterialRecord Capture(const Record& source,size_t& owned){
    MaterialRecord out;const auto& layout=source.Schema();out.tag=layout.name;out.version=layout.version;out.bytes=source.Bytes();Charge(out.bytes.size(),owned);
    for(uint32_t i=0;i<layout.fieldCount;++i){const auto& field=layout.fields[i];const std::string kind=field.kind;
        if(kind=="record"||kind=="bytes")continue;
        Charge(sizeof(Property)+std::strlen(field.name)+kind.size(),owned);
        if(kind=="reference"){out.references.push_back({field.name,field.referenceTag,source.Ref(field.name)});continue;}
        Property value;value.name=field.name;value.kind=kind;
        if(kind=="float"){
            // Some versions use +inf as an unused flipbook reciprocal. Preserve
            // that documented sentinel as bits, never as a nonfinite sample.
            const bool reciprocal=value.name=="uv_flipbook_col_fraction"||value.name=="uv_flipbook_row_fraction";
            uint32_t bits=0;if(reciprocal){if(field.offset>out.bytes.size()||out.bytes.size()-field.offset<4)throw Error("Truncated M3 effect reciprocal");for(unsigned j=0;j<4;++j)bits|=uint32_t(out.bytes[field.offset+j])<<(8*j);}
            if(reciprocal&&bits==0x7f800000){value.kind="float_positive_infinity_sentinel";value.value=uint64_t(bits);}
            else value.value=source.Float(field.name);
        }else if(kind=="int16"||kind=="int32")value.value=int64_t(source.Int(field.name));
        else if(kind=="uint64")value.value=source.UInt64(field.name);
        else if(kind=="uint8"||kind=="uint16"||kind=="uint32")value.value=uint64_t(source.UInt(field.name));
        else throw Error("Unsupported M3 effect scalar kind: "+kind);
        out.properties.push_back(std::move(value));
    }return out;
}
}
EffectsData DecodeEffects(const Document& doc,const ModelData& model){
    EffectsData out;const auto root=doc.Model();std::vector<Reference> roots;
    size_t decoded=0;
    auto definitions=[&](const char* name,const char* tag,std::vector<EffectDefinition>& target,bool material){
        const auto ref=root.Has(name)?root.Ref(name):Reference{};roots.push_back(ref);
        if(ref.count>4096)throw Error(std::string("M3 effect root exceeds 4096 records: ")+name);
        if(ref.count)doc.Resolve(ref,tag);
        for(uint32_t i=0;i<ref.count;++i){const auto record=doc.At(ref,i);EffectDefinition item;
            item.bone=record.UInt("bone");if(item.bone>=model.skeleton.size())throw Error("M3 effect bone exceeds model skeleton");
            if(material){item.material=record.UInt("material_reference_index");if(item.material>=model.materials)throw Error("M3 effect material exceeds model material references");}
            item.record=Capture(record,decoded);
            if(std::string_view(tag)=="PAR_"&&record.Has("emit_shape_regions")){
                const auto regions=record.Ref("emit_shape_regions");
                if(regions.count>4096)throw Error("M3 mesh emitter exceeds 4096 region references");
                if(regions.count){
                    const auto& values=doc.Resolve(regions,"U32_");Charge(size_t(regions.count)*sizeof(uint32_t),decoded);
                    for(uint32_t j=0;j<regions.count;++j){
                        const auto index=doc.U32(values.offset+size_t(j)*4);
                        if(model.divisions.empty()||index>=model.divisions[0].regions.size())throw Error("M3 mesh emitter references an unavailable region");
                        item.meshRegions.push_back(index);
                    }
                }
            }
            target.push_back(std::move(item));
        }
    };
    definitions("particle_systems","PAR_",out.particles,true);
    definitions("particle_copies","PARC",out.copies,false);
    definitions("ribbons","RIB_",out.ribbons,true);
    // Retain all dependency payloads once per section, growing only when a
    // later reference requests more records. No borrowed Document pointers.
    std::unordered_map<uint32_t,size_t> visited;std::vector<Reference> pending=roots;size_t retained=0;
    while(!pending.empty()){
        const auto ref=pending.back();pending.pop_back();if(!ref.count)continue;
        const auto& section=doc.Resolve(ref);if(!section.layout)throw Error("Unknown M3 effect dependency layout: "+section.tag);
        auto found=visited.find(ref.section);uint32_t first=0;size_t index;
        if(found!=visited.end()){index=found->second;first=out.payloads[index].count;if(first>=ref.count)continue;}
        else {if(out.payloads.size()>=65536)throw Error("M3 effects exceed 65536 dependency sections");index=out.payloads.size();visited.emplace(ref.section,index);out.payloads.push_back({ref.section,0,section.version,section.tag,{}});}
        const size_t extra=size_t(ref.count-first)*section.layout->size;Charge(extra,retained);
        auto& payload=out.payloads[index];const size_t bytes=size_t(ref.count)*section.layout->size;const auto* begin=doc.Data(section.offset,bytes);payload.bytes.assign(begin,begin+bytes);payload.count=ref.count;
        bool hasReferences=false;for(uint32_t f=0;f<section.layout->fieldCount;++f)if(std::string_view(section.layout->fields[f].kind)=="reference")hasReferences=true;
        if(hasReferences)for(uint32_t i=first;i<ref.count;++i){const auto record=doc.At(ref,i);for(uint32_t f=0;f<record.Schema().fieldCount;++f){const auto& field=record.Schema().fields[f];if(std::string_view(field.kind)=="reference"){if(pending.size()>=1000000)throw Error("M3 effect dependency queue exceeds budget");pending.push_back(record.Ref(field.name));}}}
    }
    return out;
}
}
