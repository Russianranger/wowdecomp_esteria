#include "ASECM3/Attachments.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>
namespace ASECM3 {
int32_t WoWAttachmentId(const std::string& name){
    static constexpr struct {const char* name;int32_t id;} names[]={
        {"ref_shield",0},{"ref_weapon right",1},{"ref_weapon left",2},{"ref_forearm right",3},{"ref_forearm left",4},
        {"ref_shoulder right",5},{"ref_shoulder left",6},{"ref_knee right",7},{"ref_knee left",8},{"ref_head",11},
        {"ref_back",12},{"ref_face",17},{"ref_overhead",18},{"ref_origin",19},{"ref_head top",20},{"ref_hand left",21},
        {"ref_hand right",22},{"ref_weapon 01",23},{"ref_hardpoint",23},{"ref_macguffin",23},{"ref_weapon 02",24},
        {"ref_hardpoint 01",24},{"ref_target01",25},{"ref_weapon",26},{"ref_waist back",28},{"ref_mount",29},
        {"ref_waist left",32},{"ref_waist right",33},{"ref_chest",34},{"ref_foot left",47},{"ref_foot right",48},
        {"ref_center",50},{"ref_waist front",53},{"ref_target",56}
    };
    auto key=name;for(auto& c:key)c=char(std::tolower(static_cast<unsigned char>(c)));
    for(const auto& entry:names)if(key==entry.name)return entry.id;return -1;
}
AttachmentData DecodeAttachments(const Document& doc){
    AttachmentData out;const auto root=doc.Model();const auto points=root.Ref("attachment_points"),addon=root.Ref("attachment_points_addon"),rests=root.Ref("bone_rests");const auto bones=root.Ref("bones").count;
    if(points.count>4096||addon.count>4096)throw Error("M3 attachment metadata exceeds 4096 records");
    size_t nameBytes=0;std::unordered_set<int32_t> used;
    for(uint32_t i=0;i<points.count;++i){const auto r=doc.At(points,i);AttachmentPoint p;p.sourceIndex=i;p.sourceValue=r.Int("unknown00");p.bone=r.UInt("bone");
        const auto name=r.Ref("name");if(name.count>1024*1024-nameBytes)throw Error("M3 attachment names exceed 1 MiB");nameBytes+=name.count;p.name=doc.String(name);p.wowId=WoWAttachmentId(p.name);
        if(p.bone>=bones){p.reason="source bone out of range";p.wowId=-1;}
        else if(p.wowId<0)p.reason="no Wrath attachment-ID mapping";
        else if(!used.insert(p.wowId).second){p.reason="Wrath attachment ID already used";p.wowId=-1;}
        if(p.bone<bones&&p.bone<rests.count){const auto rest=doc.At(rests,p.bone);Matrix inverse;
            for(unsigned c=0;c<4;++c)for(unsigned row=0;row<4;++row)inverse.At(row,c)=rest.Float(std::string("matrix.")+"xyzw"[c]+"."+"xyzw"[row]);
            p.bindPivot=TransformPoint(InverseAffine(inverse),{0,0,0});p.bindValid=true;
        }
        if(!p.reason.empty())out.warnings.push_back(p.name+": "+p.reason);
        if(p.sourceValue!=-1)out.warnings.push_back(p.name+": unknown ATT_ value retained");
        out.points.push_back(std::move(p));
    }
    if(addon.count){const auto& section=doc.Resolve(addon,"U16_");for(uint32_t i=0;i<addon.count;++i)out.addon.push_back(doc.U16(section.offset+size_t(i)*2));
        if(addon.count!=points.count)out.warnings.push_back("Attachment addon count differs from point count; retained without interpretation");
        if(std::any_of(out.addon.begin(),out.addon.end(),[](auto v){return v!=0xFFFF;}))out.warnings.push_back("Non-default attachment addon values retained without interpretation");}
    return out;
}
namespace {
bool FrameValid(const Matrix& value){for(auto v:value.values)if(!std::isfinite(v))return false;try{InverseAffine(value);return true;}catch(const Error&){return false;}}
}
AttachmentPose EvaluateAttachments(const AttachmentData& data,const ModelData& model,const Pose& pose){
    AttachmentPose out;if(!data.error.empty())return out;
    if(!pose.world.empty()&&FrameValid(pose.world[0])){out.root=pose.world[0];out.rootReady=true;}
    for(const auto& p:data.points){if(p.wowId<0)continue;AttachmentFrame f;f.id=uint32_t(p.wowId);f.sourceIndex=p.sourceIndex;
        if(p.bone<pose.world.size()&&p.bone<model.skeleton.size()&&FrameValid(pose.world[p.bone])){
            if(p.bindValid)f.bindPivot=p.bindPivot;
            else{Matrix inverse;for(unsigned i=0;i<16;++i)inverse.values[i]=model.skeleton[p.bone].inverseBind[i];f.bindPivot=TransformPoint(InverseAffine(inverse),{0,0,0});}
            f.transform=pose.world[p.bone];f.ready=true;
        }
        out.points.push_back(f);
    }
    return out;
}
}
