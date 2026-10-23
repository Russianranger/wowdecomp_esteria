#include "AttachmentRegistry.h"
#include "Engine/Source/Model2/M2Model.h"
namespace ASECM3::Client {
bool AnimateModelAttachments(CM2Model* parent){
    const auto snapshot=CaptureModelAttachments(parent);if(!snapshot)return false;
    for(auto* child=parent->m_attachList;child;child=child->m_attachNext){
        const auto point=SnapshotAttachment(*snapshot,child->m_attachmentId,(child->m_fFlags&0x40000)!=0);if(!point.ready)continue;
        const auto matrix=point.local*parent->m_matrixF4;if(!ApplyPendingAttachmentPosition(child,parent,matrix))continue;
        if(child->m_fFlags&0x1000)child->AnimateMTSimple(&matrix,parent->m_diffuseColor,parent->m_emissiveColor,parent->m_effectiveAlpha,parent->m_alpha);
        else child->AnimateMT(&matrix,parent->m_diffuseColor,parent->m_emissiveColor,parent->m_effectiveAlpha,parent->m_alpha);
        RecordAttachmentChild();
    }
    return true;
}
}
