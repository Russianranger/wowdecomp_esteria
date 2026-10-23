#include "ASECM3/Events.h"
#include <algorithm>
#include <map>
#include <limits>
namespace ASECM3 {
std::vector<EventOccurrence> EventsBetween(const AnimationBank& bank,int32_t group,const Animation& clip,uint64_t previous,uint64_t current,bool includeInitial){
    if(current<previous)throw Error("M3 event clock moved backwards without reset");
    if(group<0||size_t(group)>=bank.groups.size())throw Error("M3 event group is unavailable");
    if(clip.end<clip.start)throw Error("M3 event clip has an invalid timeline");
    struct Choice{const EventTrack* track;uint32_t priority;};std::map<uint32_t,Choice> tracks;
    for(auto index:bank.groups[group].collections){if(index>=bank.collections.size())throw Error("M3 event collection is unavailable");const auto& c=bank.collections[index];for(const auto& event:c.events){auto found=tracks.find(event.first);if(found==tracks.end()||c.priority>=found->second.priority)tracks[event.first]={&event.second,c.priority};}}
    const uint64_t duration=uint64_t(clip.end)-clip.start;const bool loop=duration&&!(clip.flags&1);
    const uint64_t first=loop?previous/duration:0,last=loop?current/duration:0;
    if(last-first>4096)throw Error("M3 event catch-up exceeds 4096 cycles");
    std::vector<EventOccurrence> out;size_t retained=0;
    for(uint64_t cycle=first;;++cycle){
        for(const auto& entry:tracks)for(const auto& event:entry.second.track->events){
            if(event.time<0||uint64_t(event.time)<clip.start||uint64_t(event.time)>clip.end)continue;
            const uint64_t offset=uint64_t(event.time)-clip.start;
            if(loop&&cycle>(std::numeric_limits<uint64_t>::max()-offset)/duration)continue;
            const auto time=loop?cycle*duration+offset:offset;
            if(time>current||!(time>previous||(includeInitial&&previous==0&&time==0)))continue;
            if(out.size()>=4096)throw Error("M3 event dispatch exceeds 4096 occurrences");
            retained+=event.raw.size()+event.name.size()+event.payload.size()+sizeof(EventOccurrence);if(retained>8u*1024u*1024u)throw Error("M3 event dispatch exceeds 8 MiB");
            out.push_back({time,cycle,entry.first,event});
        }
        if(cycle==last)break;
    }
    std::stable_sort(out.begin(),out.end(),[](const auto& a,const auto& b){if(a.elapsed!=b.elapsed)return a.elapsed<b.elapsed;return a.cycle<b.cycle;});
    return out;
}
}
