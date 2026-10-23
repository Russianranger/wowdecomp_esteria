#pragma once
#include "Effects.h"
#include "Animation.h"
namespace ASECM3 {
struct EffectEmitterLink {uint32_t meshEmitter=0,bankEmitter=0,meshBone=0,bankBone=0;};
struct EffectBankBinding {std::vector<EffectEmitterLink> particles;};
// Compatibility check for an explicitly declared effect dependency. It is not
// discovery/activation authority and never replaces the skeletal animation bank.
EffectBankBinding BindEffectBank(const ModelData& mesh,const EffectsData& meshEffects,
    const ModelData& bank,const EffectsData& bankEffects,const AnimationBank& bankAnimation);
}
