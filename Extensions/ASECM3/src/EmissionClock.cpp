#include "ASECM3/EmissionClock.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace ASECM3 {
namespace {
constexpr uint64_t ExactClockLimit=uint64_t(1)<<53;
void ClockRange(uint64_t time){if(time>ExactClockLimit)throw Error("M3 emission clock exceeds exact timestamp range");}
}
EmissionClock::EmissionClock(const Scene& scene,uint32_t emitter,uint32_t sequence){
    const auto& clips=scene.EffectSequences();
    if(sequence>=clips.size())throw Error("M3 emission clip is unavailable");
    const auto& clip=clips[sequence];const auto group=scene.EffectSequenceGroup(sequence);
    if(group<0)throw Error("M3 emission clip has no unique transformation group");
    if(clip.end<clip.start||clip.end>uint32_t(INT32_MAX))throw Error("M3 emission clip has an invalid timeline");
    m_duration=clip.end-clip.start;m_loop=m_duration&&!(clip.flags&1);
    const auto* track=scene.EffectScalarTrack(emitter,"emit_rate",group);
    const auto& record=scene.Effects().particles.at(emitter).record;
    const auto* property=record.Find("emit_rate.header.interpolation");
    const auto* mode=property?std::get_if<uint64_t>(&property->value):nullptr;
    if(!mode||*mode>1)throw Error("Unsupported M3 emission rate interpolation");
    std::vector<uint32_t> cuts{clip.start,clip.end};
    if(track){
        if(track->times.size()!=track->values.size())throw Error("M3 emission rate track is inconsistent");
        if(track->times.size()>65536)throw Error("M3 emission rate exceeds 65536 control points");
        for(auto time:track->times)if(time>int32_t(clip.start)&&time<int32_t(clip.end))cuts.push_back(uint32_t(time));
    }
    std::sort(cuts.begin(),cuts.end());cuts.erase(std::unique(cuts.begin(),cuts.end()),cuts.end());
    for(size_t i=1;i<cuts.size();++i){
        const double left=scene.SampleEffectScalar(emitter,"emit_rate",group,int32_t(cuts[i-1]));
        const double right=*mode?scene.SampleEffectScalar(emitter,"emit_rate",group,int32_t(cuts[i])):left;
        if(!std::isfinite(left)||!std::isfinite(right)||left<0||right<0)throw Error("M3 emission rate is negative or non-finite");
        m_segments.push_back({double(cuts[i-1]-clip.start),double(cuts[i]-clip.start),left,right,m_cycleIntegral});
        m_cycleIntegral+=(left+right)*.5*double(cuts[i]-cuts[i-1])*.001;
        if(!std::isfinite(m_cycleIntegral)||m_cycleIntegral>double(ExactClockLimit))throw Error("M3 emission rate integral exceeds exact count range");
    }
    const auto* counts=scene.EffectCountTrack(emitter,"emit_count",group);
    if(!counts){
        const auto* initial=record.Find("emit_count.default");
        const auto* value=initial?std::get_if<int64_t>(&initial->value):nullptr;
        if(!value||*value!=0)throw Error("M3 static emission count requires explicit activation semantics");
    }
    if(counts){
        if(counts->times.size()!=counts->values.size())throw Error("M3 emission count track is inconsistent");
        if(counts->times.size()>65536)throw Error("M3 emission count exceeds 65536 keys");
        for(size_t i=0;i<counts->times.size();++i){
            const auto time=counts->times[i];
            if(time<int32_t(clip.start)||time>int32_t(clip.end))continue;
            if(counts->values[i]<0)throw Error("M3 emission count key is negative");
            if(counts->values[i])m_counts.push_back({uint32_t(time)-clip.start,uint16_t(counts->values[i])});
        }
        std::stable_sort(m_counts.begin(),m_counts.end(),[](const Count& a,const Count& b){return a.offset<b.offset;});
    }
}
double EmissionClock::Within(double offset)const{
    if(offset<=0||m_segments.empty())return 0;
    if(offset>=m_duration)return m_cycleIntegral;
    const auto found=std::upper_bound(m_segments.begin(),m_segments.end(),offset,[](double time,const Segment& s){return time<s.end;});
    if(found==m_segments.end())return m_cycleIntegral;
    const double t=(offset-found->start)*.001,duration=(found->end-found->start)*.001;
    return found->cumulative+found->left*t+(found->right-found->left)*t*t/(2*duration);
}
double EmissionClock::IntegratedRate(uint64_t elapsed)const{
    ClockRange(elapsed);
    const double result=m_loop?double(elapsed/m_duration)*m_cycleIntegral+Within(double(elapsed%m_duration)):Within(double(std::min<uint64_t>(elapsed,m_duration)));
    if(!std::isfinite(result)||result>double(ExactClockLimit))throw Error("M3 emission integral exceeds exact count range");
    return result;
}
double EmissionClock::BirthTime(uint64_t ordinal)const{
    if(!ordinal||m_cycleIntegral<=0)throw Error("M3 continuous emission ordinal is unavailable");
    double target=double(ordinal),cycle=0;
    if(m_loop){cycle=std::floor(target/m_cycleIntegral);target-=cycle*m_cycleIntegral;if(target<=0){cycle-=1;target=m_cycleIntegral;}}
    for(const auto& segment:m_segments){
        const double duration=(segment.end-segment.start)*.001;
        const double area=(segment.left+segment.right)*.5*duration;
        if(area<=0||target>segment.cumulative+area)continue;
        const double needed=std::max(0.0,target-segment.cumulative);
        const double slope=(segment.right-segment.left)/duration;
        double t;
        if(slope==0)t=segment.left>0?needed/segment.left:0;
        else{
            const double root=std::sqrt(std::max(0.0,segment.left*segment.left+2*slope*needed));
            const double denominator=segment.left+root;
            t=denominator>0?2*needed/denominator:0;
        }
        return cycle*m_duration+segment.start+std::clamp(t,0.0,duration)*1000;
    }
    // Roundoff at the last cumulative boundary can leave target a few ulps
    // above the last area. It represents that cycle's final emission only.
    if(std::abs(target-m_cycleIntegral)<=std::numeric_limits<double>::epsilon()*std::max(1.0,m_cycleIntegral)*8)return (cycle+1)*m_duration;
    throw Error("M3 emission integral cannot resolve its birth timestamp");
}
std::vector<ParticleBirth> EmissionClock::Advance(uint64_t current){
    ClockRange(current);
    if(current<m_previous)throw Error("M3 emission clock moved backwards without Reset");
    const auto first=m_loop?m_previous/m_duration:0,last=m_loop?current/m_duration:0;
    if(last-first>4096)throw Error("M3 emission catch-up exceeds 4096 cycles");
    const uint64_t oldCount=uint64_t(std::floor(IntegratedRate(m_previous))),newCount=uint64_t(std::floor(IntegratedRate(current)));
    if(newCount<oldCount||newCount-oldCount>4096)throw Error("M3 emission catch-up exceeds 4096 rate births");
    std::vector<ParticleBirth> out;out.reserve(size_t(newCount-oldCount));
    for(uint64_t n=oldCount+1;n<=newCount;++n)out.push_back({BirthTime(n),1,false});
    for(uint64_t cycle=first;;++cycle){
        for(const auto& key:m_counts){
            const uint64_t elapsed=cycle*uint64_t(m_duration)+key.offset;
            if(elapsed>current||!(elapsed>m_previous||(m_initial&&m_previous==0&&elapsed==0)))continue;
            if(out.size()>=4096)throw Error("M3 emission catch-up exceeds 4096 occurrences");
            out.push_back({double(elapsed),key.quantity,true});
        }
        if(cycle==last)break;
    }
    std::stable_sort(out.begin(),out.end(),[](const ParticleBirth& a,const ParticleBirth& b){return a.elapsedMs<b.elapsedMs;});
    m_previous=current;m_initial=false;return out;
}
}
