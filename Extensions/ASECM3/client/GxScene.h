#pragma once
#include "ASECM3/Scene.h"
#include "ASECM3/ClothSimulation.h"
#include "ASECM3/RigidSimulation.h"
#include "ASECM3/Semantics.h"
#include "ASECM3/Picking.h"
#include "ASECM3/ParticleEmitter.h"
#include "ASECM3/RibbonTrail.h"
#include "GxDDS.h"
#include "GxSurface.h"
#include <functional>
#include <algorithm>
#include <unordered_map>
class C3Vector;
class C44Matrix;
namespace ASECM3::Client {
// Render-thread-owned adapter. Geometry/material/animation data stay in Scene.
class GxGeometryTransform;
struct SceneEnvironment;
struct WorldDrawPacket {MaterialPacket passes;Vec3 lower,upper;int32_t particleEmitter=-1,ribbon=-1;};
class GxScene final {
    std::shared_ptr<const Scene> m_scene;
    SceneFrame m_frame;
    std::vector<bool> m_regionVisible;
    std::unique_ptr<ClothSimulation> m_cloth;
    std::unique_ptr<RigidSimulation> m_rigid;
    std::vector<std::unique_ptr<ParticleEmitter>> m_particles;
    int32_t m_effectSequence=-1;
    uint64_t m_effectElapsed=0;
    double m_effectFraction=0;
    Matrix m_particleWorld=Matrix::Identity();
    Matrix m_particlePreviousWorld=Matrix::Identity();
    std::string m_particleError;
    std::vector<Material> m_particleMaterials;
    GxSurface m_particleSurface;
    std::string m_particleDrawError;
    uint64_t m_particleDraws=0;
    uint64_t m_worldParticleDraws=0;
    uint64_t m_particleBirthSamples=0;
    uint64_t m_particleSurfaceSamples=0;
    std::vector<std::vector<ParticleState>> m_worldParticles;
    std::vector<std::unique_ptr<RibbonTrail>> m_ribbons;
    std::vector<Material> m_ribbonMaterials;
    std::string m_physicsError;
    std::string m_rigidError;
    std::string m_eventError,m_lastEvent;
    uint64_t m_eventsDelivered=0,m_simulateEvents=0;
    bool m_includeEventStart=true,m_sourceEventsEnabled=true,m_rigidFromEvent=false;
    std::unordered_map<std::string,std::unique_ptr<GxDDS>> m_textures;
    std::vector<std::string> m_textureErrors;
    // Client-only, explicitly approximate previews; source material decoding
    // and its errors remain intact in Scene.
    std::unordered_map<uint32_t,Material> m_diffuseRecovery;
    size_t m_textureCursor=0;
    int32_t m_sequence=-1;
    bool m_loopSequence=false;
    uint64_t m_elapsed=0;
    double m_fraction=0;
    std::vector<LocalTransform> m_blendFrom;
    double m_blendElapsed=0;
    uint32_t m_blendMs=0;
    std::string m_animationError;
    std::vector<ActionMapping> m_actions;
    std::string m_actionError;
    uint64_t m_drawCalls=0,m_texturedDrawCalls=0,m_poseSamples=0;
    uint64_t m_compositeDrawCalls=0;
    uint64_t m_bufferShaderDraws=0;
    uint64_t m_diffuseRecoveryDraws=0;
    size_t m_materialPasses=0;
    GxSurface m_surface;
    std::unique_ptr<MaterialEvaluator> m_worldValues;
    std::vector<MaterialPass> m_worldPasses;
    std::vector<WorldDrawPacket> m_worldPackets;
    PickingIndex m_picking;
    bool m_pickDirty=true;
    SceneFrame EvaluateFrame(uint64_t elapsed) const;
    void DrawParticlePreview(const C3Vector& eye,const C3Vector& center,float scale);
    void DrawParticleWorld(uint32_t emitter,const Matrix* worldToCamera,const SceneEnvironment* environment);
    void DrawRibbonWorld(uint32_t ribbon,const Matrix* worldToCamera,const SceneEnvironment* environment);
    int32_t EffectSourceTime()const;
    void AppendWorldParticlePackets();
    void AdvanceParticleEmitters(uint64_t previousElapsed,const SceneFrame& previousFrame);
    void ParticleDrawFailure(const std::exception& error);
    void DrawGeometry(const C3Vector& eye,const C3Vector& center,float modelScale,bool lit,const GxGeometryTransform* geometry,const SceneEnvironment* environment=nullptr,int32_t packet=-1,const Matrix* particleCamera=nullptr);
public:
    explicit GxScene(std::shared_ptr<const Scene> scene,const std::string& model="");
    // One owned texture snapshot per call. Provider runs on the client thread.
    void LoadNextTexture(const std::function<std::vector<uint8_t>(const std::string&)>& provider);
    bool IsSimulationRegion(uint32_t region) const;
    bool RegionVisible(uint32_t region) const;
    void SetRegionVisible(uint32_t region,bool visible);
    void SetSequence(int32_t sourceIndex,bool loop=false);
    void SetWoWAction(uint32_t action);
    void SetPhysicsEnabled(bool enabled);
    void SetRigidEnabled(bool enabled);
    void SetEffectSequence(int32_t sequence);
    int32_t EffectSequence()const{return m_effectSequence;}
    size_t EffectParticles()const{size_t count=0;for(const auto& p:m_particles)count+=p->Alive();return count;}
    size_t EffectRibbons()const{return m_ribbons.size();}
    uint64_t EffectSpawned()const{uint64_t count=0;for(const auto& p:m_particles)count+=p->Spawned();return count;}
    uint64_t EffectDropped()const{uint64_t count=0;for(const auto& p:m_particles)count+=p->Dropped();return count;}
    const std::string& ParticleError()const{return m_particleError;}
    const std::string& ParticleDrawError()const{return m_particleDrawError;}
    uint64_t ParticleDraws()const{return m_particleDraws;}
    uint64_t WorldParticleDraws()const{return m_worldParticleDraws;}
    uint64_t ParticleBirthSamples()const{return m_particleBirthSamples;}
    uint64_t ParticleSurfaceSamples()const{return m_particleSurfaceSamples;}
    bool IncludeWorldParticleBounds(Vec3& lower,Vec3& upper,bool found);
    const std::vector<ActionMapping>& Actions() const { return m_actions; }
    const std::string& ActionError() const { return m_actionError; }
    void Advance(double seconds);
    void SetClothWorldTransform(const Matrix& transform){if(m_cloth)m_cloth->SetWorldTransform(transform);m_particleWorld=transform;}
    bool FrameBounds(Vec3& lower,Vec3& upper) const;
    bool Pick(const PickRay& ray,uint32_t nativeMode,PickHit& hit);
    bool CollisionBounds(Vec3& lower,Vec3& upper)const;
    AttachmentPose AttachmentFrames()const{return EvaluateAttachments(m_scene->Attachments(),m_scene->Model(),m_frame.pose);}
    const Scene& Data() const { return *m_scene; }
    const ClothSimulation* Cloth() const { return m_cloth.get(); }
    const RigidSimulation* Rigid() const { return m_rigid.get(); }
    const std::string& RigidError() const { return m_rigidError; }
    const std::string& EventError() const { return m_eventError; }
    const std::string& LastEvent() const { return m_lastEvent; }
    uint64_t EventsDelivered() const { return m_eventsDelivered; }
    uint64_t SimulationEvents() const { return m_simulateEvents; }
    bool SourceEventsEnabled() const { return m_sourceEventsEnabled; }
    bool RigidFromEvent() const { return m_rigidFromEvent; }
    const std::string& PhysicsError() const { return m_physicsError; }
    std::string Status() const;
    uint64_t DrawCalls() const { return m_drawCalls; }
    uint64_t TexturedDrawCalls() const { return m_texturedDrawCalls; }
    uint64_t CompositeDrawCalls() const { return m_compositeDrawCalls; }
    uint64_t BufferShaderDraws() const { return m_bufferShaderDraws; }
    size_t MaterialPasses() const { return m_materialPasses; }
    uint64_t LayerShaderDraws() const { return m_surface.Draws(); }
    uint64_t NormalMapDraws() const { return m_surface.NormalDraws(); }
    uint64_t SpecularDraws() const { return m_surface.SpecularDraws(); }
    uint64_t EnvironmentDraws() const { return m_surface.EnvironmentDraws(); }
    uint64_t CubeDraws() const { return m_surface.CubeDraws(); }
    uint64_t SphereDraws() const { return m_surface.SphereDraws(); }
    uint64_t WorldEnvironmentDraws() const { return m_surface.WorldDraws()+m_particleSurface.WorldDraws(); }
    uint64_t WorldFogDraws() const { return m_surface.FogDraws()+m_particleSurface.FogDraws(); }
    uint64_t WorldPointLightDraws() const { return m_surface.PointDraws()+m_particleSurface.PointDraws(); }
    uint64_t VerticesPacked() const { return m_surface.VerticesPacked()+m_particleSurface.VerticesPacked(); }
    uint64_t IndicesSubmitted() const { return m_surface.IndicesSubmitted()+m_particleSurface.IndicesSubmitted(); }
    size_t CubeTexturesReady() const;
    size_t InvalidDDSUploads() const;
    uint64_t ShaderStateRestores() const { return m_surface.StateRestores()+m_particleSurface.StateRestores(); }
    uint64_t ShaderStateFailures() const { return m_surface.StateFailures()+m_particleSurface.StateFailures(); }
    uint64_t PoseSamples() const { return m_poseSamples; }
    uint64_t ElapsedMs() const { return m_elapsed; }
    uint32_t TransitionMs() const { return m_blendMs; }
    double TransitionWeight() const { return m_blendMs?std::min(1.0,m_blendElapsed/m_blendMs):1.0; }
    int32_t CurrentSequence() const { return m_sequence; }
    size_t LoadedTextures() const { return m_textures.size(); }
    size_t UploadedTextures() const;
    uint64_t UploadLatches() const;
    const std::vector<WorldDrawPacket>& PrepareWorldPackets(bool mergeOpaque=false);
    void DrawCameraSpace(const GxGeometryTransform& modelToCamera,bool lit,const SceneEnvironment* environment=nullptr,int32_t packet=-1,const Matrix* particleCamera=nullptr);
    void Draw(const C3Vector& eye,const C3Vector& center,float modelScale,bool lit);
};
}
