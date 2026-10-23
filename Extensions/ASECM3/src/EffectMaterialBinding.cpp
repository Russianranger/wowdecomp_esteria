#include "ASECM3/EffectMaterialBinding.h"
#include <map>
#include <set>
#include <tuple>
namespace ASECM3 {
namespace {
using Signature=std::map<std::string,std::pair<uint32_t,uint32_t>>; // field -> ID,type
uint32_t UInt(const MaterialRecord& record,const std::string& field){const auto* p=record.Find(field);const auto* value=p?std::get_if<uint64_t>(&p->value):nullptr;if(!value||*value>UINT32_MAX)throw Error("Invalid effect material reference: "+field);return uint32_t(*value);}
Signature References(const MaterialRecord& record){
    Signature out;const std::string suffix=".header.id";
    for(const auto& p:record.properties)if(p.name.size()>suffix.size()&&p.name.compare(p.name.size()-suffix.size(),suffix.size(),suffix)==0){
        if(p.kind!="uint32")throw Error("Effect material animation ID is not uint32");
        const auto field=p.name.substr(0,p.name.size()-suffix.size());uint32_t type=0;
        const auto* value=record.Find(field+".default");
        if(value){if(value->kind=="float")type=5;else if(value->kind=="uint32")type=10;else if(value->kind=="int16")type=7;else if(value->kind=="uint16")type=8;else if(value->kind=="int32")type=9;else if(value->kind=="uint8")type=6;}
        else if(record.Find(field+".default.r"))type=4;
        else if(record.Find(field+".default.x"))type=record.Find(field+".default.w")?3:record.Find(field+".default.z")?2:1;
        if(!type)throw Error("Unknown effect material reference value type: "+field);
        if(UInt(record,field+".header.interpolation")>UINT16_MAX)throw Error("Invalid effect material interpolation");
        if(!out.emplace(field,std::make_pair(UInt(record,p.name),type)).second)throw Error("Duplicate effect material reference field");
    }
    return out;
}
using Key=std::tuple<std::string,uint32_t,std::string,uint32_t>;
struct Owner {Key key;MaterialRecordLocation location;const MaterialRecord* record;Signature signature;};
std::vector<Owner> Owners(const MaterialCatalog& catalog){
    std::vector<Owner> out;
    for(uint32_t mi=0;mi<catalog.materials.size();++mi){const auto& material=catalog.materials[mi];
        if(material.name.size()>1024)throw Error("Effect material owner name exceeds 1024 bytes");
        const auto add=[&](MaterialRecordLocation location,std::string role,uint32_t occurrence,const MaterialRecord& record){
            if(out.size()>=65536)throw Error("Effect material binding exceeds 65536 records");
            if(role.size()>2048)throw Error("Effect material record role exceeds budget");
            out.push_back({{material.name,material.type,std::move(role),occurrence},std::move(location),&record,References(record)});
        };
        add({mi,0,MaterialRecordKind::Root,""},"root",0,material.record);
        std::map<std::string,uint32_t> occurrences;
        for(uint32_t i=0;i<material.layers.size();++i){const auto& layer=material.layers[i];add({mi,i,MaterialRecordKind::Layer,layer.role},"layer:"+layer.role,occurrences[layer.role]++,layer.record);}
        occurrences.clear();
        for(uint32_t i=0;i<material.composite.size();++i){const auto& child=material.composite[i];if(child.materialReference>=catalog.materials.size())throw Error("Effect composite target is unavailable");const auto& target=catalog.materials[child.materialReference];const auto role="child:"+target.name+":"+std::to_string(target.type);add({mi,i,MaterialRecordKind::Composite,target.name},role,occurrences[role]++,child.record);}
    }
    return out;
}
}
const MaterialRecord& LocateMaterialRecord(const Material& material,const MaterialRecordLocation& location){
    if(location.kind==MaterialRecordKind::Root)return material.record;
    if(location.kind==MaterialRecordKind::Layer){if(location.index>=material.layers.size()||material.layers[location.index].role!=location.role)throw Error("Effect material layer location changed");return material.layers[location.index].record;}
    if(location.index>=material.composite.size())throw Error("Effect composite location is unavailable");return material.composite[location.index].record;
}
const MaterialRecord& LocateMaterialRecord(const MaterialCatalog& catalog,const MaterialRecordLocation& location){
    if(location.material>=catalog.materials.size())throw Error("Effect material location is unavailable");const auto& material=catalog.materials[location.material];
    if(location.kind==MaterialRecordKind::Composite){if(location.index>=material.composite.size())throw Error("Effect composite location is unavailable");const auto& child=material.composite[location.index];if(child.materialReference>=catalog.materials.size()||catalog.materials[child.materialReference].name!=location.role)throw Error("Effect composite target changed");}
    return LocateMaterialRecord(material,location);
}
EffectMaterialBinding BindEffectMaterials(const MaterialCatalog& mesh,const MaterialCatalog& bank,const AnimationBank& animation){
    std::set<uint64_t> active;
    for(const auto& c:animation.collections){for(const auto* channels:{&c.transforms,&c.materialValues})for(const auto& track:*channels)active.insert(track.first);for(const auto& track:c.unsignedValues)active.insert((uint64_t(10)<<32)|track.first);for(const auto& track:c.signed16Values)active.insert((uint64_t(7)<<32)|track.first);}
    for(const auto& c:animation.collections)for(const auto& channel:c.unsupportedChannels)active.insert((uint64_t(channel.second)<<32)|channel.first);
    const auto meshOwners=Owners(mesh),bankOwners=Owners(bank);std::map<Key,std::vector<const Owner*>> targets;std::set<std::pair<std::string,uint32_t>> materials;
    for(const auto& owner:meshOwners){targets[owner.key].push_back(&owner);materials.insert({std::get<0>(owner.key),std::get<1>(owner.key)});}
    EffectMaterialBinding out;std::set<Key> used;size_t fields=0;
    for(const auto& source:bankOwners){
        std::vector<EffectMaterialField> incoming;
        for(const auto& field:source.signature)if(active.count((uint64_t(field.second.second)<<32)|field.second.first))incoming.push_back({field.first,field.second.first,field.second.second});
        if(incoming.empty()||!materials.count({std::get<0>(source.key),std::get<1>(source.key)}))continue; // bank can contain other skin families
        if(std::get<0>(source.key).empty())throw Error("Animated effect material owner has no source name");
        const auto found=targets.find(source.key);
        if(found==targets.end()||found->second.size()!=1)throw Error("Effect material record has an absent or ambiguous mesh owner");
        const auto& target=*found->second.front();
        if(source.record->tag!=target.record->tag||source.record->version!=target.record->version||source.signature!=target.signature)throw Error("Effect material owner reference signatures disagree");
        if(!used.insert(source.key).second)throw Error("Multiple effect material records target one mesh owner");
        for(const auto& field:incoming){if(field.type>=6)throw Error("Integer material overlays require typed runtime support");if(UInt(*source.record,field.name+".header.interpolation")!=UInt(*target.record,field.name+".header.interpolation"))throw Error("Effect material interpolation policies disagree");}
        fields+=incoming.size();if(fields>65536)throw Error("Effect material binding exceeds 65536 active fields");
        out.records.push_back({target.location,source.location,std::move(incoming)});
    }
    return out;
}
EffectMaterialBinding BindEmbeddedEffectMaterials(const MaterialCatalog& catalog,const AnimationBank& animation){
    std::set<uint64_t> active;
    for(const auto& c:animation.collections){
        for(const auto* channels:{&c.transforms,&c.materialValues})for(const auto& track:*channels)active.insert(track.first);
        for(const auto& track:c.unsignedValues)active.insert((uint64_t(10)<<32)|track.first);
        for(const auto& track:c.signed16Values)active.insert((uint64_t(7)<<32)|track.first);
        for(const auto& channel:c.unsupportedChannels)active.insert((uint64_t(channel.second)<<32)|channel.first);
    }
    EffectMaterialBinding out;size_t fields=0;
    for(const auto& owner:Owners(catalog)){
        std::vector<EffectMaterialField> incoming;
        for(const auto& field:owner.signature)if(active.count((uint64_t(field.second.second)<<32)|field.second.first)){
            if(field.second.second>=6)throw Error("Integer embedded material overlays require typed runtime support");
            incoming.push_back({field.first,field.second.first,field.second.second});
        }
        if(incoming.empty())continue;
        fields+=incoming.size();if(fields>65536)throw Error("Embedded effect material binding exceeds 65536 active fields");
        out.records.push_back({owner.location,owner.location,std::move(incoming)});
    }
    return out;
}
}
