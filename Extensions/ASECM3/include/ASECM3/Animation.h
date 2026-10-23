#pragma once
#include "Document.h"
#include <array>
#include <unordered_map>
namespace ASECM3 {
using KeyValue=std::array<float,4>;
struct TransformTrack {
    uint32_t id=0,type=0,flags=0,end=0;
    std::vector<int32_t> times;
    std::vector<KeyValue> values;
};
struct UnsignedTrack {
    uint32_t id=0,flags=0,end=0;
    std::vector<int32_t> times;
    std::vector<uint32_t> values; // no float round-trip for state/flag IDs
};
struct Signed16Track {
    uint32_t id=0,flags=0,end=0;
    std::vector<int32_t> times;
    // Preserve source key order and duplicates. Emission scheduling must decide
    // key-crossing/loop semantics; decoding does not sum or resample bursts.
    std::vector<int16_t> values; // SDS6/I16_: retain signs and authored counts
};
struct AnimationEvent {
    int32_t time=0,id=-1,bone=-1;
    uint32_t flags=0,param0=0,param1=0,version=0;
    std::string name,payload;
    std::array<float,16> matrix{};
    std::vector<uint8_t> raw;
};
struct EventTrack {
    uint32_t id=0,flags=0,end=0;
    std::vector<AnimationEvent> events;
};
struct TransformCollection {
    std::string name;
    bool concurrent=false;
    uint32_t priority=0;
    std::unordered_map<uint64_t,TransformTrack> transforms;
    // Typed non-skeletal values; kept separate so pose/bank binding cannot
    // accidentally interpret a material scalar/color as a bone transform.
    std::unordered_map<uint64_t,TransformTrack> materialValues;
    std::unordered_map<uint32_t,UnsignedTrack> unsignedValues;
    std::unordered_map<uint32_t,Signed16Track> signed16Values;
    std::unordered_map<uint32_t,EventTrack> events;
    std::vector<std::pair<uint32_t,uint32_t>> unsupportedChannels; // ID,type; not silently treated as absent
    // Explicit accounting: material/event/emitter tracks are not silently
    // described as supported transform playback.
    uint32_t otherChannelTypes[13]={};
};
struct AnimationGroup { std::string name; std::vector<uint32_t> collections; };
struct AnimationBank {
    std::vector<TransformCollection> collections;
    std::vector<AnimationGroup> groups;
};
AnimationBank DecodeAnimationBank(const Document& document);
// Sampling is independent of the clip/global clock policy. The caller supplies
// the appropriate source timestamp and the owning reference interpolation mode.
KeyValue SampleTransform(const TransformTrack& track,int32_t time,uint32_t interpolation,const KeyValue& fallback);
uint32_t SampleUnsigned(const UnsignedTrack& track,int32_t time,uint32_t fallback);
}
