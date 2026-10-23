#pragma once
#include "Animation.h"
namespace ASECM3 {
struct EventOccurrence {
    uint64_t elapsed=0,cycle=0;
    uint32_t track=0;
    AnimationEvent event;
};
// Inclusive initial start, then (previous,current]. Distinct simultaneous keys
// remain distinct. Times refer to the owning source bank's sequence timeline.
std::vector<EventOccurrence> EventsBetween(const AnimationBank& bank,int32_t group,
    const Animation& clip,uint64_t previous,uint64_t current,bool includeInitial=false);
}
