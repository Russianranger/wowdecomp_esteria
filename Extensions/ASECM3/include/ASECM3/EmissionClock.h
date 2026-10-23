#pragma once
#include "Scene.h"
namespace ASECM3 {
struct ParticleBirth {
    double elapsedMs=0;
    uint32_t quantity=1;
    bool countKey=false;
};
// One explicitly selected effect owner/clip. Owns its compiled rate curve and
// count keys; no borrowed Scene, GPU, actor or skeletal-animation state.
class EmissionClock final {
    struct Segment {double start=0,end=0,left=0,right=0,cumulative=0;};
    struct Count {uint32_t offset=0;uint16_t quantity=0;};
    std::vector<Segment> m_segments;
    std::vector<Count> m_counts;
    uint32_t m_duration=0;
    bool m_loop=false,m_initial=true;
    uint64_t m_previous=0;
    double m_cycleIntegral=0;
    double Within(double offset)const;
    double BirthTime(uint64_t ordinal)const;
public:
    EmissionClock(const Scene& scene,uint32_t meshEmitter,uint32_t effectSequence);
    double IntegratedRate(uint64_t elapsedMs)const;
    // Initial count keys once, then (previous,current]. Bounded and transactional:
    // an invalid/catch-up error does not consume the previous clock position.
    std::vector<ParticleBirth> Advance(uint64_t elapsedMs);
    void Reset(){m_previous=0;m_initial=true;}
};
}
