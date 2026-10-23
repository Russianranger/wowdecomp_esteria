#include "GxScene.h"
#include "ASECM3/ParticleBillboards.h"
#include "ASECM3/ParticleSpace.h"
#include "ASECM3/ParticleTransformInterval.h"
#include "ASECM3/EmissionSurfaceInterval.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/DrawTrace.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/lua-5.1/src/asecDiagnostics.h"
#include <cctype>
namespace ASECM3::Client {
namespace {std::string Key(std::string name){for(auto& c:name)c=c=='/'?'\\':char(std::tolower(static_cast<unsigned char>(c)));return name;}}
void GxScene::AdvanceParticleEmitters(uint64_t previousElapsed,const SceneFrame& previousFrame){
    const ParticleTransformInterval history(double(previousElapsed),double(m_effectElapsed),m_particlePreviousWorld,m_particleWorld);
    uint64_t sampled=0,meshSampled=0;
    for(auto& emitter:m_particles){
        std::unique_ptr<EmissionSurfaceInterval> surface;
        std::unique_ptr<ParticleTransformInterval> bone;
        const ParticleBirthSampler sample=[&](double time,uint32_t& random,ParticleBirthSample& out){
            const auto& definition=m_scene->Effects().particles.at(emitter->SourceEmitter());
            out.modelToWorld=history.At(time);bool found=false;
            if(emitter->MeshEmission()){
                if(!surface)surface=std::make_unique<EmissionSurfaceInterval>(definition,double(previousElapsed),double(m_effectElapsed),previousFrame,m_frame);
                found=surface->Sample(time,random,out.point);if(found)++meshSampled;
            }else{
                if(!bone)bone=std::make_unique<ParticleTransformInterval>(double(previousElapsed),double(m_effectElapsed),previousFrame.pose.world.at(definition.bone),m_frame.pose.world.at(definition.bone));
                found=emitter->SamplePrimitive(time,random,bone->At(time),out.point);
            }
            if(found)++sampled;return found;
        };
        try{emitter->AdvanceSampled(m_effectElapsed,sample);}
        catch(const Error& e){throw Error("M3 particle owner "+std::to_string(emitter->SourceEmitter())+": "+e.what());}
    }
    m_particlePreviousWorld=m_particleWorld;
    if(!m_ribbons.empty()){
        const auto group=m_scene->EffectSequenceGroup(uint32_t(m_effectSequence));const auto time=EffectSourceTime();
        for(auto& ribbon:m_ribbons){
            const auto bone=m_scene->Effects().ribbons.at(ribbon->SourceRibbon()).bone;
            ribbon->Advance(m_effectElapsed,group,time,Multiply(m_particleWorld,m_frame.pose.world.at(bone)));
        }
    }
    m_particleBirthSamples+=sampled;
    m_particleSurfaceSamples+=meshSampled;
}
void GxScene::ParticleDrawFailure(const std::exception& error){
    if(!m_particleDrawError.empty())return;
    m_particleDrawError=error.what();const auto detail="M3 particle pass disabled; bank="+m_scene->EffectAnimationSource()+" error="+m_particleDrawError;ASEC_LuaDiagnostic(nullptr,detail.c_str(),0,0,0,__FILE__,__LINE__);
}
void GxScene::DrawParticlePreview(const C3Vector& eye,const C3Vector& center,float scale){
    GxDrawSourceScope sourceFamily(GxDrawSourceFamily::Particle);
    if(m_effectSequence<0||!m_particleDrawError.empty())return;
    auto* device=g_theGxDevicePtr;if(!device||!device->m_context)return;
    try{
        const auto& view=device->m_xforms[GxXform_View];const auto& matrix=view.m_mtx[view.m_level];
        const Vec3 right{matrix.a0,matrix.b0,matrix.c0},up{matrix.a1,matrix.b1,matrix.c1};
        for(size_t i=0;i<m_particles.size();++i){
            const auto snapshot=m_particles[i]->Snapshot(m_particleWorld);
            if(snapshot.empty())continue;
            const auto quads=BuildParticleBillboards(snapshot,right,up);if(quads.indices.empty())continue;
            // Each emitter needs its own override map even when it shares the
            // same source material with another emitter in this preview pass.
            MaterialEvaluator defaults(m_scene->MaterialAnimation(),-1,0);
            const auto& material=m_particleMaterials.at(i);uint32_t blend=0;
            m_scene->ApplyEffectMaterialValues(defaults,uint32_t(m_effectSequence),m_effectElapsed,m_scene->Effects().particles.at(m_particles[i]->SourceEmitter()).material,&material);
            if(const auto* p=material.record.Find("blend_mode"))blend=uint32_t(std::get<uint64_t>(p->value));
            MaterialPass pass;pass.blend=blend;
            const auto before=m_particleSurface.Draws();
            if(m_particleSurface.Draw(quads,material,pass,defaults,eye,center,scale,false,[&](const std::string& name)->GxDDS*{const auto found=m_textures.find(Key(name));return found==m_textures.end()?nullptr:found->second.get();}))m_particleDraws+=m_particleSurface.Draws()-before;
            else if(!m_particleSurface.ErrorText().empty())throw Error(m_particleSurface.ErrorText());
        }
    }catch(const std::exception& e){ParticleDrawFailure(e);}
}
bool GxScene::IncludeWorldParticleBounds(Vec3& lower,Vec3& upper,bool found){
    if(m_effectSequence<0||!m_particleDrawError.empty())return found;
    Vec3 lo=lower,hi=upper;bool any=found;
    try{
        for(const auto& emitter:m_particles){Vec3 a,b;if(!ParticleSourceBounds(emitter->Snapshot(m_particleWorld),m_particleWorld,a,b))continue;
            if(!any){lo=a;hi=b;any=true;}else{lo={std::min(lo.x,a.x),std::min(lo.y,a.y),std::min(lo.z,a.z)};hi={std::max(hi.x,b.x),std::max(hi.y,b.y),std::max(hi.z,b.z)};}}
    }catch(const std::exception& e){ParticleDrawFailure(e);return found;}
    for(const auto& ribbon:m_ribbons){Vec3 a,b;if(!ribbon->WorldBounds(a,b))continue;
        // The trail is kept in world space; the packet bounds are in source space.
        const auto inverse=InverseAffine(m_particleWorld);
        for(unsigned corner=0;corner<8;++corner){const auto p=TransformPoint(inverse,Vec3{corner&1?b.x:a.x,corner&2?b.y:a.y,corner&4?b.z:a.z});
            if(!any){lo=hi=p;any=true;}else{lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};}}}
    if(any){lower=lo;upper=hi;}return any;
}
void GxScene::AppendWorldParticlePackets(){
    m_worldParticles.clear();
    if(m_effectSequence<0||!m_particleDrawError.empty())return;
    try{
        std::vector<std::vector<ParticleState>> snapshots(m_particles.size());std::vector<WorldDrawPacket> packets;
        for(size_t i=0;i<m_particles.size();++i){
            auto& snapshot=snapshots[i];snapshot=m_particles[i]->Snapshot(m_particleWorld);
            WorldDrawPacket packet;if(!ParticleSourceBounds(snapshot,m_particleWorld,packet.lower,packet.upper))continue;
            const auto& material=m_particleMaterials.at(i);uint32_t blend=0;int32_t priority=0;
            if(const auto* p=material.record.Find("blend_mode"))blend=uint32_t(std::get<uint64_t>(p->value));
            if(const auto* p=material.record.Find("priority"))priority=int32_t(std::visit([](auto v){return double(v);},p->value));
            packet.particleEmitter=int32_t(i);packet.passes.batch=UINT32_MAX;packet.passes.deferred=blend!=0;packet.passes.priority=priority;packets.push_back(packet);
        }
        for(size_t i=0;i<m_ribbons.size();++i){
            Vec3 a,b;if(!m_ribbons[i]->WorldBounds(a,b))continue;
            const auto inverse=InverseAffine(m_particleWorld);WorldDrawPacket packet;bool first=true;
            for(unsigned corner=0;corner<8;++corner){const auto p=TransformPoint(inverse,Vec3{corner&1?b.x:a.x,corner&2?b.y:a.y,corner&4?b.z:a.z});
                if(first){packet.lower=packet.upper=p;first=false;}else{packet.lower={std::min(packet.lower.x,p.x),std::min(packet.lower.y,p.y),std::min(packet.lower.z,p.z)};packet.upper={std::max(packet.upper.x,p.x),std::max(packet.upper.y,p.y),std::max(packet.upper.z,p.z)};}}
            const auto& material=m_ribbonMaterials.at(i);uint32_t blend=0;int32_t priority=0;
            if(const auto* p=material.record.Find("blend_mode"))blend=uint32_t(std::get<uint64_t>(p->value));
            if(const auto* p=material.record.Find("priority"))priority=int32_t(std::visit([](auto v){return double(v);},p->value));
            packet.ribbon=int32_t(i);packet.passes.batch=UINT32_MAX;packet.passes.deferred=true;packet.passes.priority=priority;packets.push_back(packet);
        }
        m_worldParticles=std::move(snapshots);m_worldPackets.insert(m_worldPackets.end(),packets.begin(),packets.end());
    }catch(const std::exception& e){ParticleDrawFailure(e);}
}
void GxScene::DrawParticleWorld(uint32_t emitter,const Matrix* view,const SceneEnvironment* environment){
    GxDrawSourceScope sourceFamily(GxDrawSourceFamily::Particle);
    if(m_effectSequence<0||!m_particleDrawError.empty())return;
    try{
        if(!view)throw Error("M3 world particle packet has no scene camera transform");
        const auto camera=CameraParticles(m_worldParticles.at(emitter),*view);
        const auto quads=BuildParticleBillboards(camera,{1,0,0},{0,1,0});if(quads.indices.empty())return;
        const auto& material=m_particleMaterials.at(emitter);MaterialPass pass;
        if(const auto* p=material.record.Find("blend_mode"))pass.blend=uint32_t(std::get<uint64_t>(p->value));
        MaterialEvaluator defaults(m_scene->MaterialAnimation(),-1,0);const auto before=m_particleSurface.Draws();
        m_scene->ApplyEffectMaterialValues(defaults,uint32_t(m_effectSequence),m_effectElapsed,m_scene->Effects().particles.at(m_particles.at(emitter)->SourceEmitter()).material,&material);
        if(m_particleSurface.Draw(quads,material,pass,defaults,C3Vector(0,0,0),C3Vector(0,0,0),1,false,[&](const std::string& name)->GxDDS*{const auto found=m_textures.find(Key(name));return found==m_textures.end()?nullptr:found->second.get();},nullptr,environment))m_worldParticleDraws+=m_particleSurface.Draws()-before;
        else if(!m_particleSurface.ErrorText().empty())throw Error(m_particleSurface.ErrorText());
    }catch(const std::exception& e){ParticleDrawFailure(e);}
}
int32_t GxScene::EffectSourceTime()const{
    const auto& clip=m_scene->EffectSequences().at(size_t(m_effectSequence));
    const uint64_t duration=uint64_t(clip.end)-clip.start;
    const uint64_t offset=clip.flags&1?std::min(m_effectElapsed,duration):duration?m_effectElapsed%duration:0;
    return int32_t(clip.start+offset);
}
void GxScene::DrawRibbonWorld(uint32_t ribbon,const Matrix* view,const SceneEnvironment* environment){
    GxDrawSourceScope sourceFamily(GxDrawSourceFamily::Particle);
    if(m_effectSequence<0||!m_particleDrawError.empty()||ribbon>=m_ribbons.size())return;
    try{
        if(!view)throw Error("M3 ribbon packet has no scene camera transform");
        const auto region=m_ribbons[ribbon]->Build(*view,m_scene->EffectSequenceGroup(uint32_t(m_effectSequence)),EffectSourceTime());
        if(region.indices.empty())return;
        const auto& material=m_ribbonMaterials.at(ribbon);MaterialPass pass;
        if(const auto* p=material.record.Find("blend_mode"))pass.blend=uint32_t(std::get<uint64_t>(p->value));
        MaterialEvaluator defaults(m_scene->MaterialAnimation(),-1,0);const auto before=m_particleSurface.Draws();
        m_scene->ApplyEffectMaterialValues(defaults,uint32_t(m_effectSequence),m_effectElapsed,m_scene->Effects().ribbons.at(m_ribbons[ribbon]->SourceRibbon()).material,&material);
        if(m_particleSurface.Draw(region,material,pass,defaults,C3Vector(0,0,0),C3Vector(0,0,0),1,false,[&](const std::string& name)->GxDDS*{const auto found=m_textures.find(Key(name));return found==m_textures.end()?nullptr:found->second.get();},nullptr,environment))m_worldParticleDraws+=m_particleSurface.Draws()-before;
        else if(!m_particleSurface.ErrorText().empty())throw Error(m_particleSurface.ErrorText());
    }catch(const std::exception& e){ParticleDrawFailure(e);}
}
}
