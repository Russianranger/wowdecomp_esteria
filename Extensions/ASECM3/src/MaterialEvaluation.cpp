#include "ASECM3/MaterialEvaluation.h"
#include <algorithm>
#include <cmath>
namespace ASECM3 {
namespace {
double Value(const MaterialRecord& record,const std::string& name,double fallback){
    const auto* p=record.Find(name);return p?std::visit([](auto v){return double(v);},p->value):fallback;
}
}
bool LayerClampsColor(const MaterialRecord& record){
    const double flags=Value(record,"flags",0),adapter=Value(record,"asec_color_clamp",0);
    if(!std::isfinite(flags)||flags<0||flags>UINT32_MAX||std::floor(flags)!=flags||!std::isfinite(adapter))throw Error("Invalid M3 layer color-clamp control");
    return (uint32_t(flags)&0x20)!=0||adapter>.5;
}
MaterialEvaluator::MaterialEvaluator(const AnimationBank& bank,int32_t group,int32_t sourceTime):m_time(sourceTime){
    if(group<-1||(group>=0&&size_t(group)>=bank.groups.size()))throw Error("M3 material animation group is unavailable");
    if(group<0)return;
    std::unordered_map<uint64_t,uint32_t> priorities;
    for(const auto index:bank.groups[group].collections){
        if(index>=bank.collections.size())throw Error("M3 material animation collection is unavailable");
        const auto& collection=bank.collections[index];
        for(const auto* channels:{&collection.transforms,&collection.materialValues})for(const auto& item:*channels){
            auto found=priorities.find(item.first);
            if(found==priorities.end()||collection.priority>=found->second){priorities[item.first]=collection.priority;m_tracks[item.first]=&item.second;}
        }
    }
}
const TransformTrack* MaterialEvaluator::Track(uint32_t type,uint32_t id)const{const auto found=m_tracks.find((uint64_t(type)<<32)|id);return found==m_tracks.end()?nullptr:found->second;}
void MaterialEvaluator::AddOverride(const MaterialRecord& owner,const std::string& field,const TransformTrack& track,int32_t time){
    if(!owner.Find(field+".header.id")||uint32_t(Value(owner,field+".header.id",0))!=track.id)throw Error("Material overlay reference ID changed");
    if(!m_overrides[&owner].emplace(field,Override{&track,time}).second)throw Error("Duplicate material overlay owner/field");
}
KeyValue MaterialEvaluator::Sample(const MaterialRecord& record,const std::string& field,uint32_t type,const KeyValue& fallback) const {
    KeyValue initial=fallback;
    if(type==5)initial[0]=float(Value(record,field+".default",fallback[0]));
    else for(unsigned i=0;i<(type==1?2u:type==2?3u:4u);++i)initial[i]=float(Value(record,field+".default."+(type==4?"rgba":"xyzw")[i],fallback[i]));
    const auto* id=record.Find(field+".header.id");if(!id)return initial;
    const auto owner=m_overrides.find(&record);
    if(owner!=m_overrides.end()){const auto fieldValue=owner->second.find(field);if(fieldValue!=owner->second.end()){const auto& value=fieldValue->second;if(value.track->type!=type||value.track->id!=uint32_t(Value(record,field+".header.id",0)))throw Error("Material overlay field type/ID changed");return SampleTransform(*value.track,value.time,uint32_t(Value(record,field+".header.interpolation",0)),initial);}}
    const auto key=(uint64_t(type)<<32)|uint32_t(Value(record,field+".header.id",0));
    const auto found=m_tracks.find(key);
    return found==m_tracks.end()?initial:SampleTransform(*found->second,m_time,uint32_t(Value(record,field+".header.interpolation",0)),initial);
}
float MaterialEvaluator::Scalar(const MaterialRecord& record,const std::string& field,float fallback) const {
    const auto v=Sample(record,field,5,{fallback,0,0,0})[0];
    if(!std::isfinite(v))throw Error("Non-finite M3 animated material scalar: "+field);
    return v;
}
std::vector<MaterialPass> EvaluateMaterialPasses(const MaterialCatalog& catalog,const MaterialEvaluator& values,uint32_t division){
    std::vector<MaterialPass> fixed,transparent;
    for(uint32_t bi=0;bi<catalog.batches.size();++bi){
        const auto& batch=catalog.batches[bi];if(batch.division!=division)continue;
        if(batch.material>=catalog.materials.size())throw Error("M3 material pass has invalid root");
        const auto& root=catalog.materials[batch.material];
        struct Pending{uint32_t material,depth;float opacity;};
        std::vector<Pending> stack{{batch.material,0,1}};
        std::vector<MaterialPass> leaves;
        while(!stack.empty()){
            auto current=stack.back();stack.pop_back();
            if(current.material>=catalog.materials.size())throw Error("M3 material pass has invalid child");
            if(current.depth>64)throw Error("M3 composite material depth exceeds 64");
            const auto& material=catalog.materials[current.material];
            if(material.type==3){
                if(stack.size()+material.composite.size()>16384)throw Error("M3 composite pending pass budget exceeded");
                for(auto i=material.composite.rbegin();i!=material.composite.rend();++i){
                    const float opacity=current.opacity*std::clamp(values.Scalar(i->record,"alpha_factor",1),0.0f,1.0f);
                    if(opacity>0)stack.push_back({i->materialReference,current.depth+1,opacity});
                }
            }else{
                const auto& renderRecord=material.type==12&&material.bufferSurface?material.bufferSurface->record:material.record;
                uint32_t blend=uint32_t(Value(renderRecord,"blend_mode",Value(material.record,"blend_mode",0)));
                // Distortion reads the scene behind it, so it must follow the opaque pass.
                if(blend==0&&(current.opacity<1||material.type==2))blend=1;
                const auto priority=int32_t(Value(root.type==3?root.record:material.record,"priority",0));
                leaves.push_back({bi,current.material,current.opacity,blend,priority,root.type==3});
                if(fixed.size()+transparent.size()+leaves.size()>16384)throw Error("M3 material pass budget exceeds 16384");
            }
        }
        const bool opaqueFirst=!leaves.empty()&&leaves.front().blend==0;
        // Terrain precedes ordinary layers; creep follows ordinary opaque ones.
        const auto rank=[&](const MaterialPass& p){const auto t=catalog.materials[p.material].type;return t==4?0:t==7?2:1;};
        std::stable_sort(leaves.begin(),leaves.end(),[&](const auto& a,const auto& b){return rank(a)<rank(b);});
        for(const auto& pass:leaves){
            if(pass.blend==0||(root.type==3&&opaqueFirst)||catalog.materials[pass.material].type==4||catalog.materials[pass.material].type==7)fixed.push_back(pass);
            else {auto deferred=pass;deferred.deferred=true;transparent.push_back(deferred);}
        }
    }
    std::stable_sort(transparent.begin(),transparent.end(),[](const auto& a,const auto& b){return a.priority>b.priority;});
    fixed.insert(fixed.end(),transparent.begin(),transparent.end());return fixed;
}
std::vector<MaterialPacket> PackMaterialPasses(const std::vector<MaterialPass>& passes){
    if(passes.size()>16384)throw Error("M3 world packet input exceeds material pass budget");
    std::vector<MaterialPacket> packets;
    for(uint32_t i=0;i<passes.size();++i){const auto& p=passes[i];
        if(packets.empty()||packets.back().batch!=p.batch||packets.back().deferred!=p.deferred||packets.back().priority!=p.priority)
            packets.push_back({i,1,p.batch,p.priority,p.deferred});
        else ++packets.back().count;
    }
    return packets;
}
}
