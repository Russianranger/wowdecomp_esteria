#pragma once
#include "Material.h"
#include "Animation.h"
namespace ASECM3 {
// Source LAYR.color_clamp (0x20) or normalized MADD adapter marker.
bool LayerClampsColor(const MaterialRecord& record);
// Borrowed track pointers require the owning Scene/AnimationBank to outlive
// this short-lived evaluation context. No client or GPU state is referenced.
class MaterialEvaluator final {
    std::unordered_map<uint64_t,const TransformTrack*> m_tracks;
    struct Override {const TransformTrack* track=nullptr;int32_t time=0;};
    std::unordered_map<const MaterialRecord*,std::unordered_map<std::string,Override>> m_overrides;
    int32_t m_time;
public:
    MaterialEvaluator(const AnimationBank& bank,int32_t group,int32_t sourceTime);
    const TransformTrack* Track(uint32_t type,uint32_t id)const;
    // Caller must establish record/field ownership before adding an overlay.
    void AddOverride(const MaterialRecord& owner,const std::string& field,const TransformTrack& track,int32_t sourceTime);
    KeyValue Sample(const MaterialRecord& record,const std::string& field,
                    uint32_t type,const KeyValue& fallback) const;
    float Scalar(const MaterialRecord& record,const std::string& field,float fallback) const;
};
struct MaterialPass {
    uint32_t batch=0,material=0;
    float opacity=1;
    uint32_t blend=0;
    int32_t priority=0;
    bool composite=false;
    bool deferred=false;
};
struct MaterialPacket {uint32_t first=0,count=0,batch=0;int32_t priority=0;bool deferred=false;};
// Preserve contiguous composite child order while exposing world queue groups.
std::vector<MaterialPacket> PackMaterialPasses(const std::vector<MaterialPass>& passes);
// Composite is a proxy: each source child produces a pass over the same batch.
// Opaque-first groups retain local source order; transparent-first groups defer
// alpha passes to the model-wide priority ordering documented by Art Tools.
std::vector<MaterialPass> EvaluateMaterialPasses(const MaterialCatalog& catalog,
                                                const MaterialEvaluator& values,uint32_t division=0);
}
