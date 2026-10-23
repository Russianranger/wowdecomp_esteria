#pragma once
#include "Document.h"
namespace ASECM3 {
struct ActionMapping {
    int32_t action=153; // Wrath Custom0 for unresolved source meaning
    bool excluded=false, global=false, manual=false;
    uint32_t weight=1;
    std::string confidence="low", reason;
};
// Name/index policies belong to the source model identity, never its bank name.
// A reordered manually indexed bank fails closed via its expected-name guard.
ActionMapping ResolveAction(const std::string& name,const std::string& model,
                            uint32_t sourceIndex,uint32_t sourceFlags=0);
std::vector<ActionMapping> ResolveActions(const std::vector<Animation>& clips,
                                         const std::string& model);
// Deterministic preview selection: highest source policy weight, then order.
// Excluded/global clips remain available through explicit raw source playback.
int32_t SelectAction(const std::vector<ActionMapping>& mappings,uint32_t action);
// World-only Run/Sprint fallback to eligible body locomotion, after exact match.
// Like SelectWorldAction, then the nearest clip of the same family (attacks, readies, spells, swim, ...), then Stand.
int32_t SelectBodyAction(const std::vector<ActionMapping>& mappings,uint32_t action);
int32_t SelectWorldAction(const std::vector<ActionMapping>& mappings,uint32_t action);
}
