#include "ASECM3/Animation.h"
#include <algorithm>
#include <cmath>
#include <map>
namespace ASECM3 {
namespace {
std::vector<uint32_t> UIntArray(const Document& doc,const Reference& ref) {
    std::vector<uint32_t> out;
    if (!ref.count) return out;
    const auto& section=doc.Resolve(ref,"U32_"); out.reserve(ref.count);
    for (uint32_t i=0;i<ref.count;++i) out.push_back(doc.U32(section.offset+size_t(i)*4));
    return out;
}
KeyValue Normalize(KeyValue q) {
    double norm=0; for (auto v:q) norm+=double(v)*v;
    if (!std::isfinite(norm) || norm<1e-16) throw Error("M3 animation has an invalid quaternion");
    const float scale=static_cast<float>(1/std::sqrt(norm));
    for (auto& v:q) v*=scale;
    return q;
}
}
AnimationBank DecodeAnimationBank(const Document& doc) {
    const auto model=doc.Model();
    const auto collections=model.Ref("sequence_transformation_collections"), groups=model.Ref("sequence_transformation_groups");
    AnimationBank out; out.collections.reserve(collections.count);
    size_t totalKeys=0,eventBytes=0,totalChannels=0;
    static const char* fields[]={"sdev","sd2v","sd3v","sd4q","sdcc","sdr3","sdu8","sds6","sdu6","sds3","sdu3","sdfg","sdmb"};
    for (uint32_t i=0;i<collections.count;++i) {
        const auto record=doc.At(collections,i);
        TransformCollection collection;
        collection.name=record.String("name"); collection.concurrent=record.UInt("concurrent")!=0; collection.priority=record.UInt("priority");
        const auto ids=UIntArray(doc,record.Ref("anim_ids")), refs=UIntArray(doc,record.Ref("anim_refs"));
        if (ids.size()!=refs.size()) throw Error("M3 STC animation ID/reference counts differ: "+collection.name);
        if(ids.size()>1000000-totalChannels)throw Error("M3 animation exceeds one million channel references");totalChannels+=ids.size();
        for (size_t j=0;j<ids.size();++j) {
            const auto type=refs[j]>>16, index=refs[j]&0xffff;
            if (type>=13) throw Error("M3 STC refers to an unknown animation channel type");
            const auto container=record.Ref(fields[type]);
            if (index>=container.count) throw Error("M3 STC animation channel index exceeds its container");
            const auto trackRecord=doc.At(container,index);
            const auto frames=trackRecord.Ref("frames"), keys=trackRecord.Ref("keys");
            if (frames.count!=keys.count) throw Error("M3 track frame/key counts differ: "+collection.name);
            if (type!=2 && type!=3) ++collection.otherChannelTypes[type];
            if (type!=0 && type!=1 && type!=2 && type!=3 && type!=4 && type!=5 && type!=7 && type!=10){collection.unsupportedChannels.push_back({ids[j],type});continue;}
            totalKeys+=frames.count;
            if (totalKeys>16000000) throw Error("M3 transform track key budget exceeded");
            if(type==0){
                EventTrack track;track.id=ids[j];track.flags=trackRecord.UInt("flags");track.end=trackRecord.UInt("fend");
                if(frames.count){const auto& times=doc.Resolve(frames,"I32_");doc.Resolve(keys,"EVNT");
                    if(frames.count>65536)throw Error("M3 event track exceeds 65536 keys");
                    for(uint32_t k=0;k<frames.count;++k){const auto key=doc.At(keys,k);if(key.Ref("name").count>1024u*1024u||key.Ref("payload").count>1024u*1024u)throw Error("M3 event string exceeds 1 MiB");AnimationEvent event;event.time=int32_t(doc.U32(times.offset+size_t(k)*4));event.version=key.Schema().version;event.name=key.String("name");event.payload=key.String("payload");event.id=key.Int("id");event.bone=key.Int("bone");event.flags=key.UInt("flags");if(key.Has("data_param0"))event.param0=key.UInt("data_param0");if(key.Has("data_param1"))event.param1=key.UInt("data_param1");
                        for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)event.matrix[c*4+r]=key.Float(std::string("matrix.")+"xyzw"[c]+"."+"xyzw"[r]);
                        event.raw=key.Bytes();eventBytes+=sizeof(AnimationEvent)+event.raw.size()+event.name.size()+event.payload.size();if(eventBytes>32u*1024u*1024u)throw Error("M3 decoded events exceed 32 MiB");track.events.push_back(std::move(event));}
                    std::stable_sort(track.events.begin(),track.events.end(),[](const auto& a,const auto& b){return a.time<b.time;});
                }collection.events[track.id]=std::move(track);continue;
            }
            if(type==7){
                Signed16Track track;track.id=ids[j];track.flags=trackRecord.UInt("flags");track.end=trackRecord.UInt("fend");
                track.times.reserve(frames.count);track.values.reserve(frames.count);
                if(frames.count){
                    const auto& times=doc.Resolve(frames,"I32_");const auto& values=doc.Resolve(keys,"I16_");
                    for(uint32_t k=0;k<frames.count;++k){
                        const auto raw=doc.U16(values.offset+size_t(k)*2);
                        // Explicit sign extension avoids an implementation-defined
                        // unsigned-to-signed conversion for negative source keys.
                        const int32_t value=raw<0x8000u?int32_t(raw):int32_t(raw)-0x10000;
                        track.times.push_back(int32_t(doc.U32(times.offset+size_t(k)*4)));
                        track.values.push_back(int16_t(value));
                    }
                }
                collection.signed16Values[track.id]=std::move(track);continue;
            }
            if(type==10){
                UnsignedTrack track;track.id=ids[j];track.flags=trackRecord.UInt("flags");track.end=trackRecord.UInt("fend");std::map<int32_t,uint32_t> ordered;
                if(frames.count){const auto& times=doc.Resolve(frames,"I32_");const auto& values=doc.Resolve(keys,"U32_");for(uint32_t k=0;k<frames.count;++k)ordered[int32_t(doc.U32(times.offset+size_t(k)*4))]=doc.U32(values.offset+size_t(k)*4);}
                for(const auto& key:ordered){track.times.push_back(key.first);track.values.push_back(key.second);}collection.unsignedValues[track.id]=std::move(track);continue;
            }
            TransformTrack track; track.id=ids[j]; track.type=type; track.flags=trackRecord.UInt("flags"); track.end=trackRecord.UInt("fend");
            std::map<int32_t,KeyValue> ordered;
            if (frames.count) {
                const auto& timeSection=doc.Resolve(frames,"I32_");
                const auto& valueSection=doc.Resolve(keys,type==1?"VEC2":type==2?"VEC3":type==3?"QUAT":type==4?"COL":"REAL");
                const auto components=type==1?2:type==2?3:type==5?1:4;
                for (uint32_t k=0;k<frames.count;++k) {
                    const auto time=static_cast<int32_t>(doc.U32(timeSection.offset+size_t(k)*4));
                    KeyValue value{};
                    if(type==4){
                        // COL is serialized BGRA; callers consume RGBA bytes.
                        const size_t p=valueSection.offset+size_t(k)*4;
                        value={float(doc.U8(p+2)),float(doc.U8(p+1)),float(doc.U8(p)),float(doc.U8(p+3))};
                    }else for (int c=0;c<components;++c) value[c]=doc.F32(valueSection.offset+size_t(k)*components*4+c*4);
                    ordered[time]=value; // last key at an identical timestamp wins
                }
            }
            track.times.reserve(ordered.size()); track.values.reserve(ordered.size());
            for (const auto& key:ordered) { track.times.push_back(key.first); track.values.push_back(key.second); }
            auto& target=type==1||type==4||type==5?collection.materialValues:collection.transforms;
            target[(uint64_t(track.type)<<32)|track.id]=std::move(track);
        }
        out.collections.push_back(std::move(collection));
    }
    out.groups.reserve(groups.count);
    for (uint32_t i=0;i<groups.count;++i) {
        const auto record=doc.At(groups,i);
        AnimationGroup group{record.String("name"),UIntArray(doc,record.Ref("stc_indices"))};
        for (auto index:group.collections) if (index>=out.collections.size()) throw Error("M3 STG collection index is invalid");
        out.groups.push_back(std::move(group));
    }
    return out;
}
KeyValue SampleTransform(const TransformTrack& track,int32_t time,uint32_t interpolation,const KeyValue& fallback) {
    if (track.times.empty()) return fallback;
    if (track.times.size()!=track.values.size()) throw Error("Native M3 track is inconsistent");
    auto finish=[&](KeyValue value){return track.type==3?Normalize(value):value;};
    if (time<=track.times.front()) return finish(track.values.front());
    if (time>=track.times.back()) return finish(track.values.back());
    const auto right=std::upper_bound(track.times.begin(),track.times.end(),time)-track.times.begin();
    const auto left=right-1;
    if (!interpolation) return finish(track.values[left]);
    if (interpolation!=1) throw Error("Unsupported M3 reference interpolation mode");
    const float amount=static_cast<float>((double(time)-track.times[left])/(double(track.times[right])-track.times[left]));
    auto a=track.values[left], b=track.values[right];
    float wa=1-amount,wb=amount;
    if (track.type==3) {
        a=Normalize(a); b=Normalize(b);
        float dot=0; for (int c=0;c<4;++c) dot+=a[c]*b[c];
        if (dot<0) { dot=-dot; for (auto& v:b) v=-v; }
        dot=std::max(-1.0f,std::min(1.0f,dot));
        if (dot<0.9995f) { const float angle=std::acos(dot), sine=std::sin(angle); wa=std::sin((1-amount)*angle)/sine; wb=std::sin(amount*angle)/sine; }
    }
    KeyValue result{}; for (int c=0;c<4;++c) result[c]=a[c]*wa+b[c]*wb;
    return finish(result);
}
uint32_t SampleUnsigned(const UnsignedTrack& track,int32_t time,uint32_t fallback){
    if(track.times.empty())return fallback;
    if(track.times.size()!=track.values.size())throw Error("M3 unsigned animation track is inconsistent");
    const auto right=std::upper_bound(track.times.begin(),track.times.end(),time);
    return right==track.times.begin()?track.values.front():track.values[size_t(right-track.times.begin()-1)];
}
}
