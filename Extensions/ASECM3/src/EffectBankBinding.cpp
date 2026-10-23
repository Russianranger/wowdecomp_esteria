#include "ASECM3/EffectBankBinding.h"
#include <map>
#include <cmath>
#include <set>
namespace ASECM3 {
namespace {
using Signature=std::map<std::string,uint32_t>;
Signature References(const MaterialRecord& record){
    Signature out;const std::string suffix=".header.id";
    for(const auto& p:record.properties)if(p.name.size()>suffix.size()&&p.name.compare(p.name.size()-suffix.size(),suffix.size(),suffix)==0){
        const auto field=p.name.substr(0,p.name.size()-suffix.size());
        if(p.kind!="uint32")throw Error("Effect animation reference ID is not uint32");
        const auto* id=std::get_if<uint64_t>(&p.value);if(!id||*id>UINT32_MAX)throw Error("Effect animation reference ID exceeds uint32");
        const auto* mode=record.Find(field+".header.interpolation");
        const auto* interpolation=mode?std::get_if<uint64_t>(&mode->value):nullptr;
        if(!interpolation||*interpolation>UINT16_MAX)throw Error("Effect animation reference interpolation is invalid");
        if(!out.emplace(field,uint32_t(*id)).second)throw Error("Duplicate effect animation reference field");
    }
    if(out.empty())throw Error("Effect animation owner has no parameter-reference signature");
    return out;
}
std::string BoneName(const ModelData& model,const EffectDefinition& e){
    if(e.bone>=model.skeleton.size())throw Error("Effect bank owner bone exceeds skeleton");
    const auto& name=model.skeleton[e.bone].name;if(name.empty())throw Error("Effect bank owner bone has no name");return name;
}
}
EffectBankBinding BindEffectBank(const ModelData& mesh,const EffectsData& meshEffects,const ModelData& bank,const EffectsData& bankEffects,const AnimationBank& animation){
    if(!meshEffects.error.empty()||!bankEffects.error.empty())throw Error("Cannot bind an effect dependency with decode errors");
    if(bankEffects.particles.empty())throw Error("Effect dependency has no particle owners");
    if(meshEffects.particles.size()>4096||bankEffects.particles.size()>4096)throw Error("Effect binding exceeds emitter budget");
    // Names alone cannot select an owner, and coincident IDs alone cannot prove
    // ownership. Match the complete field-to-ID signature on a named PAR owner.
    using Key=std::pair<std::string,Signature>;
    std::set<uint32_t> animated;
    for(const auto& c:animation.collections){for(const auto* tracks:{&c.transforms,&c.materialValues})for(const auto& track:*tracks)animated.insert(uint32_t(track.first));for(const auto& track:c.unsignedValues)animated.insert(track.first);}
    for(const auto& c:animation.collections)for(const auto& track:c.signed16Values)animated.insert(track.first);
    for(const auto& c:animation.collections)for(const auto& channel:c.unsupportedChannels)animated.insert(channel.first);
    std::map<Key,std::vector<uint32_t>> owners;
    for(uint32_t i=0;i<meshEffects.particles.size();++i){const auto& e=meshEffects.particles[i];owners[{BoneName(mesh,e),References(e.record)}].push_back(i);}
    EffectBankBinding out;std::map<uint32_t,uint32_t> used;
    for(uint32_t i=0;i<bankEffects.particles.size();++i){const auto& e=bankEffects.particles[i];const auto found=owners.find({BoneName(bank,e),References(e.record)});
        if(found==owners.end()||found->second.size()!=1)throw Error("Effect dependency has an absent or ambiguous mesh parameter owner");
        const auto index=found->second.front();const auto& target=meshEffects.particles[index];
        if(target.record.tag!=e.record.tag||target.record.version!=e.record.version)throw Error("Effect owner record types/versions disagree");
        // Inactive M3A defaults commonly serialize constant interpolation while
        // the mesh retains linear references. Only an actual incoming channel
        // needs matching sampling policy; other fields keep the mesh defaults.
        for(const auto& reference:References(e.record))if(animated.count(reference.second)){
            const auto* a=target.record.Find(reference.first+".header.interpolation");const auto* b=e.record.Find(reference.first+".header.interpolation");
            if(!a||!b||a->value!=b->value)throw Error("Animated effect parameter interpolation policies disagree");
        }
        if(!used.emplace(index,i).second)throw Error("Multiple effect-bank owners map to one mesh emitter");
        out.particles.push_back({index,i,target.bone,e.bone});
    }
    return out;
}
}
