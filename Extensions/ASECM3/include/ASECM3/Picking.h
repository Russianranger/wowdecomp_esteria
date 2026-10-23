#pragma once
#include "Scene.h"
#include "MaterialEvaluation.h"
#include "Ray.h"
namespace ASECM3 {
// Ray direction need not be normalized. Distance retains its parameterization
// through an affine inverse, including nonuniform scale and reflections.
struct PickHit {double distance=0;uint32_t frameRegion=0,triangle=0;};
std::vector<bool> PickableRegions(const SceneFrame& frame,const MaterialCatalog& catalog,
    const std::vector<MaterialPass>& passes,const std::vector<bool>& visible,uint32_t nativeMode);
// Owned coarse triangle blocks, refitted lazily by the host after a pose change.
// No engine pointers or borrowed geometry survive Refit/Intersect calls.
class PickingIndex final {
    struct Block {Vec3 lower,upper;uint32_t region=0,first=0,count=0;};
    std::vector<Block> m_blocks;
    size_t m_regions=0;
public:
    void Refit(const SceneFrame& frame);
    bool Intersect(const SceneFrame& frame,const std::vector<bool>& eligible,
                   const PickRay& ray,PickHit& hit) const;
    size_t Blocks()const{return m_blocks.size();}
};
}
