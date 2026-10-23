#pragma once
#include "Pose.h"
namespace ASECM3 {
struct AttachmentPoint {
    std::string name,reason;uint32_t bone=0,sourceIndex=0;int32_t wowId=-1,sourceValue=-1;
    Vec3 bindPivot;bool bindValid=false;
};
struct AttachmentData {
    std::vector<AttachmentPoint> points;std::vector<uint16_t> addon;
    std::vector<std::string> warnings;std::string error;
};
struct AttachmentFrame {uint32_t id=0,sourceIndex=0;Matrix transform;Vec3 bindPivot;bool ready=false;};
struct AttachmentPose {std::vector<AttachmentFrame> points;Matrix root=Matrix::Identity();bool rootReady=false;};
// Exact names and IDs from the project's existing M3->Wrath converter policy.
int32_t WoWAttachmentId(const std::string& name);
// Metadata-only decode: no geometry/animation work; usable before model loading.
AttachmentData DecodeAttachments(const Document& document);
AttachmentPose EvaluateAttachments(const AttachmentData& data,const ModelData& model,const Pose& pose);
}
