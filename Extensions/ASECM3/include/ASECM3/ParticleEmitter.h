#pragma once
#include "EmissionClock.h"
#include "EmissionSurface.h"
#include "ParticleAppearance.h"
#include <functional>
namespace ASECM3 {
struct ParticleBirthSample {EmissionPoint point;Matrix modelToWorld=Matrix::Identity();};
// Called only for an accepted birth, at its exact effect elapsed timestamp.
// A sampler must be repeatable and must not mutate external simulation state.
// It may return false for an unavailable/zero-density source surface.
using ParticleBirthSampler=std::function<bool(double,uint32_t&,ParticleBirthSample&)>;
struct ParticleState {
    uint64_t serial=0;
    double birthMs=0,age=0,lifetime=0;
    Vec3 position,velocity; // world space for both source simulation modes
    ParticleVisual visual;
};
// Native portable particle motion/lifetime owner. Explicit phase activation;
// independent of skeletal clips and of GPU/material state.
class ParticleEmitter final {
    struct Particle {
        uint64_t serial=0;
        double birthMs=0,lifetime=0,drag=0,gravity=0,visualScale=1;
        Vec3 origin,velocity;
        ParticleAppearance appearance;
    };
    std::shared_ptr<const Scene> m_scene;
    uint32_t m_emitter=0,m_sequence=0,m_capacity=0,m_random=0,m_visualSeed=0;
    bool m_worldSpace=false,m_meshEmission=true;
    EmissionClock m_clock;
    uint64_t m_previous=0,m_spawned=0,m_dropped=0,m_noSurface=0;
    std::vector<Particle> m_particles;
    void AdvanceImpl(uint64_t elapsedMs,const SceneFrame* frame,const Matrix& modelToWorld,const ParticleBirthSampler* sampler);
public:
    ParticleEmitter(std::shared_ptr<const Scene> scene,uint32_t emitter,uint32_t sequence,uint32_t seed=0x6d2b79f5u);
    // The caller supplies the current posed source mesh and model-to-world
    // transform. One posed surface is built lazily per nonempty update.
    // Existing particles expire at each birth's time before applying the cap.
    void Advance(uint64_t elapsedMs,const SceneFrame& frame,const Matrix& modelToWorld=Matrix::Identity());
    // Explicit history/source-time lane. The sampler owns pose/transform lookup;
    // no previous-frame interpolation or physics rewind is invented here.
    void AdvanceSampled(uint64_t elapsedMs,const ParticleBirthSampler& sampler);
    std::vector<ParticleState> Snapshot(const Matrix& modelToWorld=Matrix::Identity())const;
    uint32_t Capacity()const{return m_capacity;}
    uint32_t SourceEmitter()const{return m_emitter;}
    bool MeshEmission()const{return m_meshEmission;}
    bool SamplePrimitive(double elapsedMs,uint32_t& random,const Matrix& bone,EmissionPoint& output)const;
    bool WorldSpace()const{return m_worldSpace;}
    uint64_t Spawned()const{return m_spawned;}
    uint64_t Dropped()const{return m_dropped;}
    uint64_t NoSurface()const{return m_noSurface;}
    size_t Alive()const{return m_particles.size();}
    uint64_t ElapsedMs()const{return m_previous;}
};
}
