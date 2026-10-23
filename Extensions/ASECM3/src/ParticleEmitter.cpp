#include "ASECM3/ParticleEmitter.h"
#include "ASECM3/ParticleSpace.h"
#include "ASECM3/PrimitiveEmission.h"
#include <algorithm>
#include <cmath>
#include <memory>
namespace ASECM3 {
namespace {
const Scene& Require(const std::shared_ptr<const Scene>& scene){if(!scene)throw Error("M3 particle emitter has no scene");return *scene;}
double Value(const MaterialRecord& record,const char* field,double fallback=0){
    const auto* property=record.Find(field);
    const double value=property?std::visit([](auto v){return double(v);},property->value):fallback;
    if(!std::isfinite(value))throw Error(std::string("Non-finite M3 particle parameter: ")+field);return value;
}
uint32_t UInt(const MaterialRecord& record,const char* field){
    const double value=Value(record,field);
    if(value<0||value>UINT32_MAX||std::floor(value)!=value)throw Error(std::string("Invalid M3 particle integer: ")+field);
    return uint32_t(value);
}
double Random(uint32_t& state){if(!state)state=0x6d2b79f5u;state^=state<<13;state^=state>>17;state^=state<<5;return double(state)/4294967296.0;}
Vec3 Direction(const Matrix& matrix,const Vec3& v){
    return {float(matrix.At(0,0)*v.x+matrix.At(0,1)*v.y+matrix.At(0,2)*v.z),float(matrix.At(1,0)*v.x+matrix.At(1,1)*v.y+matrix.At(1,2)*v.z),float(matrix.At(2,0)*v.x+matrix.At(2,1)*v.y+matrix.At(2,2)*v.z)};
}
void Validate(const Matrix& matrix){for(double value:matrix.values)if(!std::isfinite(value))throw Error("Non-finite M3 particle model-to-world transform");InverseAffine(matrix);}
void Validate(const Vec3& value){if(!std::isfinite(value.x)||!std::isfinite(value.y)||!std::isfinite(value.z))throw Error("Non-finite M3 particle motion");}
}
ParticleEmitter::ParticleEmitter(std::shared_ptr<const Scene> scene,uint32_t emitter,uint32_t sequence,uint32_t seed)
    :m_scene(std::move(scene)),m_emitter(emitter),m_sequence(sequence),m_random(seed),m_visualSeed(seed^0xa511e9b3u),m_clock(Require(m_scene),emitter,sequence){
    const auto& definition=m_scene->Effects().particles.at(emitter);const auto& record=definition.record;
    if(UInt(record,"particle_type")!=0)throw Error("M3 particle motion currently requires a camera-facing billboard");
    m_meshEmission=UInt(record,"emit_shape")==7;
    if(m_meshEmission){if(UInt(record,"emit_type")!=4)throw Error("Mesh particle emission requires source Mesh Normal direction");}
    else ValidatePrimitiveEmission(definition);
    m_capacity=UInt(record,"emit_max");if(m_capacity>4096)throw Error("M3 particle emitter exceeds 4096 live particles");
    m_worldSpace=record.version>=17?(UInt(record,"additional_flags")&8)!=0:UInt(record,"world_space")!=0;
    if(Value(record,"drag")<0)throw Error("M3 particle drag is negative");
    m_particles.reserve(m_capacity);
}
bool ParticleEmitter::SamplePrimitive(double elapsed,uint32_t& random,const Matrix& bone,EmissionPoint& output)const{
    if(m_meshEmission)throw Error("Mesh emitter cannot use primitive sampling");
    if(!std::isfinite(elapsed)||elapsed<0||elapsed>double(uint64_t(1)<<53))throw Error("Invalid primitive emission timestamp");
    const auto& clip=m_scene->EffectSequences().at(m_sequence);const uint32_t duration=clip.end-clip.start;
    const double offset=duration&&!(clip.flags&1)?std::fmod(elapsed,double(duration)):std::min(elapsed,double(duration));
    const int32_t time=int32_t(clip.start+uint32_t(std::floor(offset)));
    MaterialEvaluator values(*m_scene->EffectAnimation(),m_scene->EffectSequenceGroup(m_sequence),time);
    return SamplePrimitiveEmission(m_scene->Effects().particles.at(m_emitter),values,bone,random,output);
}
void ParticleEmitter::Advance(uint64_t elapsed,const SceneFrame& frame,const Matrix& world){
    AdvanceImpl(elapsed,&frame,world,nullptr);
}
void ParticleEmitter::AdvanceSampled(uint64_t elapsed,const ParticleBirthSampler& sampler){
    if(!sampler)throw Error("M3 particle birth sampler is empty");
    AdvanceImpl(elapsed,nullptr,Matrix::Identity(),&sampler);
}
void ParticleEmitter::AdvanceImpl(uint64_t elapsed,const SceneFrame* frame,const Matrix& world,const ParticleBirthSampler* sampler){
    Validate(world);
    auto clock=m_clock;const auto births=clock.Advance(elapsed);
    auto particles=m_particles;auto random=m_random;auto spawned=m_spawned,dropped=m_dropped,noSurface=m_noSurface;
    const auto& record=m_scene->Effects().particles.at(m_emitter).record;
    const auto& clip=m_scene->EffectSequences().at(m_sequence);
    const auto group=m_scene->EffectSequenceGroup(m_sequence);const uint32_t duration=clip.end-clip.start;
    const auto flags=UInt(record,"additional_flags");
    const bool speedRandom=record.version>=17?(flags&1)!=0:UInt(record,"emit_speed_randomize")!=0;
    const bool lifeRandom=record.version>=17?(flags&2)!=0:UInt(record,"lifespan_randomize")!=0;
    std::unique_ptr<MeshEmissionSurface> surface;
    const auto expire=[&](double time){particles.erase(std::remove_if(particles.begin(),particles.end(),[&](const Particle& p){return time>=p.birthMs+p.lifetime*1000;}),particles.end());};
    for(const auto& birth:births){
        expire(birth.elapsedMs);
        // Requests that exceed the live cap are dropped at their authored
        // timestamp. They must never become a delayed backlog after expiry.
        const uint32_t available=m_capacity-uint32_t(particles.size());
        const uint32_t accepted=std::min(available,birth.quantity);dropped+=birth.quantity-accepted;
        if(!accepted)continue;
        if(!sampler&&m_meshEmission&&!surface)surface=std::make_unique<MeshEmissionSurface>(m_scene->Effects().particles.at(m_emitter),*frame);
        const double offset=duration&&!(clip.flags&1)?std::fmod(birth.elapsedMs,double(duration)):std::min(birth.elapsedMs,double(duration));
        const int32_t sourceTime=int32_t(clip.start+uint32_t(std::floor(std::max(0.0,offset))));
        const double speed=m_scene->SampleEffectScalar(m_emitter,"emit_speed",group,sourceTime);
        const double alternateSpeed=speedRandom?m_scene->SampleEffectScalar(m_emitter,"emit_speed_random",group,sourceTime):speed;
        const double life=m_scene->SampleEffectScalar(m_emitter,"lifespan",group,sourceTime);
        const double alternateLife=lifeRandom?m_scene->SampleEffectScalar(m_emitter,"lifespan_random",group,sourceTime):life;
        if(life<=0||alternateLife<=0||life>3600||alternateLife>3600)throw Error("M3 particle lifespan is outside (0,3600] seconds");
        for(uint32_t j=0;j<accepted;++j){
            ParticleBirthSample sampled;
            if(sampler){if(!(*sampler)(birth.elapsedMs,random,sampled)){++noSurface;continue;}Validate(sampled.modelToWorld);}
            else{
                const bool found=m_meshEmission?surface->Sample(random,sampled.point):SamplePrimitive(birth.elapsedMs,random,frame->pose.world.at(m_scene->Effects().particles.at(m_emitter).bone),sampled.point);
                if(!found){++noSurface;continue;}sampled.modelToWorld=world;
            }
            const auto& point=sampled.point;Validate(point.position);Validate(point.normal);
            const double normalLength=double(point.normal.x)*point.normal.x+double(point.normal.y)*point.normal.y+double(point.normal.z)*point.normal.z;
            if(std::abs(normalLength-1)>1e-4)throw Error("M3 particle birth normal is not unit length");
            const double velocity=speed+(alternateSpeed-speed)*(speedRandom?Random(random):0);
            Particle p;p.serial=++spawned;p.birthMs=birth.elapsedMs;p.lifetime=life+(alternateLife-life)*(lifeRandom?Random(random):0);
            p.appearance=ParticleAppearance::Capture(record,MaterialEvaluator(*m_scene->EffectAnimation(),group,sourceTime),m_visualSeed+uint32_t(p.serial)*0x9e3779b9u);
            p.drag=Value(record,"drag");p.gravity=Value(record,"gravity");p.origin=point.position;
            p.velocity={float(point.normal.x*velocity),float(point.normal.y*velocity),float(point.normal.z*velocity)};
            if(m_worldSpace){p.origin=TransformPoint(sampled.modelToWorld,p.origin);p.velocity=Direction(sampled.modelToWorld,p.velocity);p.visualScale=ParticleUniformScale(sampled.modelToWorld);}
            Validate(p.origin);Validate(p.velocity);particles.push_back(p);
        }
    }
    expire(double(elapsed));
    m_clock=std::move(clock);m_particles=std::move(particles);m_random=random;m_spawned=spawned;m_dropped=dropped;m_noSurface=noSurface;m_previous=elapsed;
}
std::vector<ParticleState> ParticleEmitter::Snapshot(const Matrix& world)const{
    Validate(world);std::vector<ParticleState> out;out.reserve(m_particles.size());
    for(const auto& particle:m_particles){
        ParticleState p;p.serial=particle.serial;p.birthMs=particle.birthMs;p.age=(double(m_previous)-particle.birthMs)*.001;p.lifetime=particle.lifetime;
        p.visual=particle.appearance.At(p.age/p.lifetime);p.visual.size=float(p.visual.size*(m_worldSpace?particle.visualScale:ParticleUniformScale(world)));
        if(!std::isfinite(p.visual.size)||p.visual.size<0)throw Error("Invalid transformed M3 particle size");
        const double t=p.age,d=particle.drag,x=d*t;
        const double decay=std::exp(-x);
        double velocityFactor,gravityFactor;
        if(d==0){velocityFactor=t;gravityFactor=.5*t*t;}
        else{
            velocityFactor=-std::expm1(-x)/d;
            gravityFactor=std::abs(x)<1e-4?t*t*(.5-x/6+x*x/24):(t-velocityFactor)/d;
        }
        p.position={float(particle.origin.x+particle.velocity.x*velocityFactor),float(particle.origin.y+particle.velocity.y*velocityFactor),float(particle.origin.z+particle.velocity.z*velocityFactor-particle.gravity*gravityFactor)};
        p.velocity={float(particle.velocity.x*decay),float(particle.velocity.y*decay),float(particle.velocity.z*decay-particle.gravity*velocityFactor)};
        if(!m_worldSpace){p.position=TransformPoint(world,p.position);p.velocity=Direction(world,p.velocity);}
        Validate(p.position);Validate(p.velocity);out.push_back(p);
    }
    return out;
}
}
