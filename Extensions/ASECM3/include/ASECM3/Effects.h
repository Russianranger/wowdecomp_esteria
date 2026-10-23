#pragma once
#include "Material.h"
namespace ASECM3 {
// MaterialRecord is the existing portable typed source-record container.
// Using it here does not interpret emitter records as shader materials.
struct EffectDefinition {
    uint32_t bone=0,material=UINT32_MAX;
    MaterialRecord record;
    std::vector<uint32_t> meshRegions; // explicit PAR_.emit_shape_regions, division 0
};
struct EffectPayload {
    uint32_t section=0,count=0,version=0;
    std::string tag;
    std::vector<uint8_t> bytes;
};
struct EffectsData {
    std::vector<EffectDefinition> particles,copies,ribbons;
    std::vector<EffectPayload> payloads;
    std::string error;
};
// Owns records and their reference graph. This is effect input, not a claim
// that particles, copies, ribbons or their collision/actor behavior are drawn.
EffectsData DecodeEffects(const Document& document,const ModelData& model);
}
