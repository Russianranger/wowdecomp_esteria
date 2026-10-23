#include "ASECM3/Scene.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
using namespace ASECM3;
std::string Quote(const std::string& s){std::string out="\"";for(unsigned char c:s){if(c=='\\'||c=='\"')out+='\\';out+=c<32?' ':c;}return out+'\"';}
void Require(bool condition,const char* message){if(!condition)throw Error(message);}
std::vector<uint8_t> Read(const char* path){const auto n=std::filesystem::file_size(path);if(n>128u*1024*1024)throw Error("Attachment input exceeds 128 MiB");std::vector<uint8_t> bytes(n);std::ifstream f(path,std::ios::binary);if(!f.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw Error("Short attachment read");return bytes;}
unsigned Fixtures(){
    Require(WoWAttachmentId("REF_HEAD")==11&&WoWAttachmentId("Ref_Weapon Right")==1&&WoWAttachmentId("Ref_Hand Right")==22,"Exact attachment mapping failed");
    Require(WoWAttachmentId("Head")==-1&&WoWAttachmentId(" Ref_Head")==-1,"Attachment aliases were guessed");
    ModelData model;model.skeleton.resize(2);for(auto& b:model.skeleton)for(unsigned i=0;i<16;++i)b.inverseBind[i]=i%5==0?1.0f:0;
    model.skeleton[1].inverseBind[12]=-4;model.skeleton[1].inverseBind[13]=-5;model.skeleton[1].inverseBind[14]=-6;
    AttachmentData data;AttachmentPoint point;point.name="Ref_Head";point.bone=1;point.wowId=11;data.points.push_back(point);
    Pose pose;pose.world={Matrix::Identity(),Matrix::Identity()};pose.world[1].At(0,3)=10;
    auto result=EvaluateAttachments(data,model,pose);Require(result.rootReady&&result.points.size()==1&&result.points[0].ready,"Attachment pose missing");
    Require(result.points[0].transform.At(0,3)==10&&result.points[0].bindPivot.x==4&&result.points[0].bindPivot.y==5&&result.points[0].bindPivot.z==6,"Attachment bind pivot/posed frame mismatch");
    pose.world[1].At(0,3)=20;Require(EvaluateAttachments(data,model,pose).points[0].transform.At(0,3)==20,"Attachment ignored updated pose");
    pose.world[1].At(0,0)=0;Require(!EvaluateAttachments(data,model,pose).points[0].ready,"Zero-scale attachment frame was enabled");
    data.error="source mismatch";Require(EvaluateAttachments(data,model,pose).points.empty(),"Invalid attachment data was enabled");return 7;
}
int main(int argc,char** argv){try{
    if(argc==3&&std::string(argv[1])=="--name"){std::cout<<WoWAttachmentId(argv[2])<<'\n';return 0;}
    if(argc<2||argc>3)throw Error("Usage: ASECM3AttachmentsInspect model.m3 [bank.m3a]");const auto fixtures=Fixtures();
    auto bytes=Read(argv[1]);auto document=Document::Parse(bytes,false);const auto early=DecodeAttachments(document);
    const auto* field=document.Model().Schema().Find("sequences");Require(field!=nullptr,"Deferred validation fixture field missing");const auto address=document.Resolve(document.ModelReference()).offset+field->offset;
    for(unsigned i=0;i<4;++i){bytes[address+i]=i==0?1:0;bytes[address+4+i]=255;}auto deferred=Document::Parse(std::move(bytes),false);DecodeAttachments(deferred);
    bool rejected=false;try{Scene::FromDocument(std::move(deferred));}catch(const Error&){rejected=true;}Require(rejected,"Deferred reference validation was bypassed");
    auto scene=Scene::FromDocument(std::move(document));if(argc==3)scene=scene->WithAnimationBank(Read(argv[2]),argv[2]);
    const auto& data=scene->Attachments();Require(data.error.empty(),data.error.c_str());Require(early.points.size()==data.points.size(),"Early attachment metadata differs");
    unsigned poses=0,frames=0;auto sample=[&](const SceneFrame& frame){++poses;const auto values=EvaluateAttachments(data,scene->Model(),frame.pose);for(const auto& value:values.points){if(!value.ready)continue;const auto& p=data.points.at(value.sourceIndex);Require(p.bone<frame.pose.world.size(),"Attachment bone out of pose");for(unsigned i=0;i<16;++i)Require(value.transform.values[i]==frame.pose.world[p.bone].values[i],"Attachment uses a different bone pose");++frames;}};
    sample(scene->EvaluateGroup());for(uint32_t i=0;i<scene->Model().animations.size();++i)if(scene->SequenceGroup(i)>=0){const auto& clip=scene->Model().animations[i];sample(scene->EvaluateSequence(i,(clip.end-clip.start)/2));}
    std::cout.precision(9);std::cout<<"{\"ok\":true,\"fixtures\":"<<fixtures<<",\"deferredValidationRejected\":true,\"poses\":"<<poses<<",\"readyFrames\":"<<frames<<",\"points\":[";
    for(size_t i=0;i<data.points.size();++i){if(i)std::cout<<',';const auto& p=data.points[i];Require(p.name==early.points[i].name&&p.bone==early.points[i].bone&&p.wowId==early.points[i].wowId,"Early attachment point changed during full parse");std::cout<<"{\"index\":"<<p.sourceIndex<<",\"name\":"<<Quote(p.name)<<",\"bone\":"<<p.bone<<",\"sourceValue\":"<<p.sourceValue<<",\"bindPivot\":["<<p.bindPivot.x<<','<<p.bindPivot.y<<','<<p.bindPivot.z<<"],\"bindValid\":"<<(p.bindValid?"true":"false")<<",\"wowId\":"<<p.wowId<<",\"reason\":"<<Quote(p.reason)<<'}';}
    std::cout<<"],\"addon\":[";for(size_t i=0;i<data.addon.size();++i){if(i)std::cout<<',';std::cout<<data.addon[i];}std::cout<<"],\"scope\":\"Source attachment mapping and posed transforms, not live equipment/effect appearance\"}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
