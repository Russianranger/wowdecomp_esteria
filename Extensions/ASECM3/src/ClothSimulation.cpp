#include "ASECM3/ClothSimulation.h"
#include "ASECM3/ClothBending.h"
#include "ASECM3/ClothTethers.h"
#include "ASECM3/ClothContact.h"
#include "ASECM3/ClothAerodynamics.h"
#include "ASECM3/AffineMotion.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <limits>
namespace ASECM3 {
namespace {
constexpr double SolverHz=240,FixedStep=1/SolverHz,StiffnessReferenceHz=120;
constexpr unsigned ProjectionPasses=4;
// Retain the previous nominal response while allocating the same960passes/s
// to smaller integration/collision steps (NVIDIA reference-frequency policy).
double IterationStiffness(double value){return 1-std::pow(1-std::clamp(value,0.0,1.0),StiffnessReferenceHz*FixedStep/ProjectionPasses);}
Vec3 Add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3 Sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vec3 Scale(Vec3 a,double s){return {float(a.x*s),float(a.y*s),float(a.z*s)};}
double Dot(Vec3 a,Vec3 b){return double(a.x)*b.x+double(a.y)*b.y+double(a.z)*b.z;}
double Length(Vec3 a){return std::sqrt(Dot(a,a));}
Vec3 Cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
Vec3 Unit(Vec3 a,Vec3 fallback={}){const auto n=Length(a);return n>1e-12?Scale(a,1/n):fallback;}
float Coefficient(const PhysicsCloth& c,const char* name,float fallback){for(const auto& p:c.coefficients)if(p.first==name)return p.second;return fallback;}
SceneRegion& RegionAt(SceneFrame& frame,uint32_t index){for(auto& r:frame.regions)if(r.division==0&&r.region==index)return r;throw Error("Cloth output region is unavailable");}
const SceneRegion& RegionAt(const SceneFrame& frame,uint32_t index){for(const auto& r:frame.regions)if(r.division==0&&r.region==index)return r;throw Error("Cloth input region is unavailable");}
void RebuildBasis(SceneRegion& region){
    std::vector<Vec3> normals(region.vertices.size()),tangents(region.vertices.size()),bitangents(region.vertices.size());
    for(size_t i=0;i<region.indices.size();i+=3){
        const auto a=region.indices[i],b=region.indices[i+1],c=region.indices[i+2];const auto& va=region.vertices.at(a);const auto& vb=region.vertices.at(b);const auto& vc=region.vertices.at(c);
        const auto e1=Sub(vb.position,va.position),e2=Sub(vc.position,va.position),normal=Cross(e1,e2);
        for(auto v:{a,b,c})normals[v]=Add(normals[v],normal);
        if(va.uv.size()<2||vb.uv.size()<2||vc.uv.size()<2)continue;
        const double u1=vb.uv[0]-va.uv[0],v1=vb.uv[1]-va.uv[1],u2=vc.uv[0]-va.uv[0],v2=vc.uv[1]-va.uv[1],det=u1*v2-u2*v1;
        if(std::abs(det)<1e-12)continue;
        const auto t=Scale(Sub(Scale(e1,v2),Scale(e2,v1)),1/det),bt=Scale(Sub(Scale(e2,u1),Scale(e1,u2)),1/det);
        for(auto v:{a,b,c}){tangents[v]=Add(tangents[v],t);bitangents[v]=Add(bitangents[v],bt);}
    }
    for(size_t i=0;i<region.vertices.size();++i){auto& v=region.vertices[i];v.normal=Unit(normals[i],v.normal);v.tangent=Unit(Sub(tangents[i],Scale(v.normal,Dot(v.normal,tangents[i]))),v.tangent);if(Length(bitangents[i])>1e-12)v.tangentSign=Dot(Cross(v.normal,v.tangent),bitangents[i])<0?-1.0f:1.0f;}
}
}
struct ClothSimulation::State {
    struct Edge {uint32_t a,b;double rest,stiffness;};
    struct Bend {std::array<uint32_t,4> vertices;double rest,stiffness;};
    struct Cloth {
        const PhysicsCloth* source;
        std::vector<Vec3> bind,positions,velocities,previousTargets;
        std::vector<Matrix> previousCapsules,tickCapsules;
        std::vector<double> inverseMass;
        std::vector<Edge> edges;
        std::vector<Bend> bends;
        std::vector<ClothTether> tethers;
        std::vector<std::array<uint32_t,3>> triangles;
        bool initialized=false;
    };
    const Scene& scene;std::vector<Cloth> cloths;double accumulator=0,maximumDisplacement=0;
    size_t particles=0,mapped=0;uint64_t steps=0,resets=0;
    Matrix world=Matrix::Identity();bool hasWorld=false;
    double fluidDensity=0;Vec3 worldWind;
    bool diagnoseCollisions=false;ClothCollisionDiagnostics diagnostics;
    explicit State(const Scene& s):scene(s){
        if(!s.Physics().error.empty())throw Error(s.Physics().error);
        const auto& model=s.Model();if(model.divisions.empty())return;
        std::set<uint32_t> drivenRegions;
        for(const auto& source:s.Physics().cloths){
            // Bank-only PHCL can carry collision constraints without a mesh.
            if(source.influences.empty())continue;
            if(source.simulationRegion>=model.divisions[0].regions.size())throw Error("Cloth simulation region exceeds primary division");
            const auto& region=model.divisions[0].regions[source.simulationRegion];
            if(source.simulated.size()!=region.vertexCount||source.vertexBones.size()!=region.vertexCount||source.vertexWeights.size()!=region.vertexCount)throw Error("Cloth simulation vertex count does not match its region");
            if(particles+region.vertexCount>100000)throw Error("Cloth runtime exceeds 100000 particles");
            Cloth c;c.source=&source;c.inverseMass.assign(region.vertexCount,0);c.bind.reserve(region.vertexCount);
            for(uint32_t i=0;i<region.vertexCount;++i){c.bind.push_back(model.vertices.at(region.firstVertex+i).position);for(unsigned k=0;k<4;++k)if((source.vertexWeights[i]>>(k*8))&255u){const auto bone=(source.vertexBones[i]>>(k*8))&255u;if(bone>=model.skeleton.size())throw Error("Cloth particle bone exceeds mesh skeleton");}}
            const double density=Coefficient(source,"density",1);if(density<=0)throw Error("Cloth density must be positive");
            std::map<std::pair<uint32_t,uint32_t>,uint32_t> adjacent;
            std::set<std::pair<uint32_t,uint32_t>> edges,paired;
            const double bendingIteration=IterationStiffness(Coefficient(source,"stiffness_blending",.6f));
            for(size_t i=region.firstIndex;i<size_t(region.firstIndex)+region.indexCount;i+=3){
                const uint32_t a=model.divisions[0].indices.at(i),b=model.divisions[0].indices.at(i+1),d=model.divisions[0].indices.at(i+2);
                c.triangles.push_back({a,b,d});
                const double mass=Length(Cross(Sub(c.bind[b],c.bind[a]),Sub(c.bind[d],c.bind[a])))*density/6;
                for(auto v:{a,b,d})c.inverseMass[v]+=mass;
                const uint32_t v[]={a,b,d};for(unsigned e=0;e<3;++e){const auto key=std::minmax(v[e],v[(e+1)%3]);edges.insert(key);const auto prior=adjacent.find(key);
                    if(prior!=adjacent.end()){
                        if(!paired.insert(key).second)throw Error("Cloth bending requires manifold triangle edges");
                        if(prior->second==v[(e+2)%3])continue; // duplicated face has no distinct hinge
                        const std::array<uint32_t,4> vertices{key.first,key.second,prior->second,v[(e+2)%3]};double rest;
                        if(ClothBendGeometry({c.bind[vertices[0]],c.bind[vertices[1]],c.bind[vertices[2]],c.bind[vertices[3]]},rest))c.bends.push_back({vertices,rest,bendingIteration});
                    }else adjacent.emplace(key,v[(e+2)%3]);}
            }
            auto addEdges=[&](const auto& pairs,double stiffness){const auto iteration=IterationStiffness(stiffness);for(const auto& e:pairs){const auto rest=Length(Sub(c.bind[e.first],c.bind[e.second]));if(rest>1e-10)c.edges.push_back({e.first,e.second,rest,iteration});}};
            addEdges(edges,Coefficient(source,"stiffness_stretching",.8f));
            for(size_t i=0;i<c.inverseMass.size();++i){if(source.simulated[i]>1)throw Error("Unsupported cloth simulated-vertex flag");c.inverseMass[i]=source.simulated[i]&&c.inverseMass[i]>1e-12?1/c.inverseMass[i]:0;}
            std::vector<std::array<uint32_t,2>> tetherEdges;for(const auto& edge:c.edges)tetherEdges.push_back({edge.a,edge.b});
            std::vector<uint8_t> pinned(c.bind.size());for(size_t i=0;i<pinned.size();++i)pinned[i]=source.simulated[i]==0;
            c.tethers=BuildClothTethers(c.bind,tetherEdges,pinned);
            for(const auto& constraint:source.constraints)if(constraint.bone<0||size_t(constraint.bone)>=model.skeleton.size()||constraint.radius<0||constraint.height<0)throw Error("Cloth capsule references an invalid global bone or dimension");
            for(const auto& influence:source.influences){
                if(influence.simulationRegion!=source.simulationRegion||influence.influencedRegion==source.simulationRegion||influence.influencedRegion>=model.divisions[0].regions.size())throw Error("Cloth influence region does not match its simulator");
                if(!drivenRegions.insert(influence.influencedRegion).second)throw Error("Multiple cloth records drive the same output region");
                const auto n=model.divisions[0].regions[influence.influencedRegion].vertexCount;
                if(influence.lookups.size()!=n||influence.weights.size()!=n)throw Error("Cloth influence vertex count does not match output region");
                for(size_t i=0;i<n;++i)for(unsigned k=0;k<4;++k)if((influence.weights[i]>>(k*8))&255u)if(((influence.lookups[i]>>(k*16))&65535u)>=region.vertexCount)throw Error("Cloth influence references an absent simulation particle");
                mapped+=n;
            }
            particles+=region.vertexCount;cloths.push_back(std::move(c));
        }
    }
    std::vector<Vec3> Targets(const Cloth& c,const SceneFrame& frame){
        std::vector<Vec3> out(c.bind.size());const auto& source=*c.source;
        for(size_t i=0;i<out.size();++i){unsigned sum=0;Vec3 p{};for(unsigned k=0;k<4;++k){const auto w=(source.vertexWeights[i]>>(k*8))&255u;if(w){const auto b=(source.vertexBones[i]>>(k*8))&255u;p=Add(p,Scale(TransformPoint(frame.pose.skin.at(b),c.bind[i]),w));sum+=w;}}out[i]=sum?Scale(p,1.0/sum):RegionAt(frame,source.simulationRegion).vertices.at(i).position;}
        return out;
    }
    std::vector<Matrix> Capsules(const Cloth& c,const SceneFrame& frame){
        std::vector<Matrix> out;out.reserve(c.source->constraints.size());
        for(const auto& p:c.source->constraints)out.push_back(Multiply(frame.pose.skin.at(p.bone),p.local));
        return out;
    }
    void Rebase(const SceneFrame& frame){
        for(auto& c:cloths)if(c.initialized){
            const auto targets=Targets(c,frame);
            for(size_t i=0;i<targets.size();++i)c.positions[i]=Add(c.positions[i],Sub(targets[i],c.previousTargets[i]));
            c.previousTargets=targets;c.previousCapsules=Capsules(c,frame);c.tickCapsules=c.previousCapsules;
        }
    }
    void Advance(SceneFrame& frame,double seconds){
        if(!std::isfinite(seconds)||seconds<0)throw Error("Invalid cloth frame interval");
        if(seconds>.25){for(auto& c:cloths)c.initialized=false;accumulator=0;++resets;seconds=0;}
        const auto previousAccumulator=accumulator;
        Vec3 localWorldWind{};if(fluidDensity>0){const auto worldInverse=InverseAffine(world);const auto windOrigin=TransformPoint(worldInverse,{});localWorldWind=Sub(TransformPoint(worldInverse,worldWind),windOrigin);}
        accumulator+=seconds;constexpr double dt=FixedStep;const auto count=unsigned(std::min(60.0,std::floor((accumulator+1e-12)/dt)));accumulator=std::max(0.0,accumulator-count*dt);
        maximumDisplacement=0;
        for(auto& c:cloths){
            const auto targets=Targets(c,frame);
            const auto currentCapsules=Capsules(c,frame);
            if(!c.initialized){c.positions=targets;c.velocities.assign(targets.size(),{});c.previousTargets=targets;c.previousCapsules=currentCapsules;c.tickCapsules=currentCapsules;c.initialized=true;}
            struct Capsule{Matrix local,inverse,prior;float radius,height;uint32_t sourceIndex;};
            // Sample anchors and animated collision volumes at the same fixed
            // tick, including the remainder carried from the previous frame.
            // Factor each capsule interval once, not once per fixed tick.
            // Linear matrix-entry blending shrinks a rotating capsule and can
            // make it singular halfway through a half turn.
            std::vector<std::unique_ptr<AffineMotion>> motion(currentCapsules.size());
            if(count)for(size_t j=0;j<currentCapsules.size();++j){
                try{InverseAffine(c.previousCapsules[j]);InverseAffine(currentCapsules[j]);}
                catch(const Error&){continue;} // authored zero scale disables this interval
                try{motion[j]=std::make_unique<AffineMotion>(c.previousCapsules[j],currentCapsules[j]);}
                catch(const Error& e){throw Error("PHCC["+std::to_string(j)+"] motion: "+e.what());}
            }
            std::vector<Vec3> old,trackingShift(c.positions.size());old.reserve(c.positions.size());
            struct Contact{size_t particle;Vec3 normal,velocity;};std::vector<Contact> contacts;
            contacts.reserve(c.positions.size());
            for(unsigned step=0;step<count;++step){
                const double blend=seconds>0?std::clamp((dt-previousAccumulator+step*dt)/seconds,0.0,1.0):1;
                std::vector<Capsule> capsules;capsules.reserve(currentCapsules.size());
                for(size_t j=0;j<currentCapsules.size();++j){const auto& p=c.source->constraints[j];
                    if(!motion[j]){c.tickCapsules[j]=currentCapsules[j];continue;}
                    const auto local=motion[j]->At(blend);
                    try{capsules.push_back({local,InverseAffine(local),c.tickCapsules[j],p.radius,p.height,uint32_t(j)});}catch(const Error&){/* zero scale deliberately disables a source collision volume */}
                    c.tickCapsules[j]=local;}
                old=c.positions;std::fill(trackingShift.begin(),trackingShift.end(),Vec3{});contacts.clear();
                const double damp=std::exp(-std::max(0.0f,Coefficient(*c.source,"damping",0))*dt),tracking=1-std::exp(-std::max(0.0f,Coefficient(*c.source,"tracking",0))*dt);
                if(fluidDensity>0){
                    const auto air=Scale(Add(localWorldWind,c.source->localWind),Coefficient(*c.source,"wind_scale",1));
                    // Moving pins contribute their actual world-oriented velocity;
                    // zero inverse mass prevents aerodynamic motion of the pins.
                    for(size_t i=0;i<c.positions.size();++i)if(!c.inverseMass[i]){const auto target=Add(c.previousTargets[i],Scale(Sub(targets[i],c.previousTargets[i]),blend));c.velocities[i]=Scale(Sub(target,c.positions[i]),1/dt);}
                    for(const auto& triangle:c.triangles){std::array<Vec3,3> points,velocities;std::array<double,3> mass;for(unsigned j=0;j<3;++j){const auto i=triangle[j];points[j]=c.positions[i];velocities[j]=c.velocities[i];mass[j]=c.inverseMass[i];}
                        const auto impulse=ClothAirImpulse(points,velocities,mass,air,fluidDensity,Coefficient(*c.source,"drag_factor",0),Coefficient(*c.source,"lift_factor",0),dt);
                        for(unsigned j=0;j<3;++j)if(mass[j]){const auto i=triangle[j];c.velocities[i]=Add(c.velocities[i],Scale(impulse,mass[j]/3));}
                    }
                }
                for(size_t i=0;i<c.positions.size();++i){const auto target=Add(c.previousTargets[i],Scale(Sub(targets[i],c.previousTargets[i]),blend));if(!c.inverseMass[i]){c.positions[i]=target;continue;}c.velocities[i]=Scale(c.velocities[i],damp);c.velocities[i].z-=float(9.81*Coefficient(*c.source,"gravity",1)*dt);c.positions[i]=Add(c.positions[i],Scale(c.velocities[i],dt));trackingShift[i]=Scale(Sub(target,c.positions[i]),tracking);c.positions[i]=Add(c.positions[i],trackingShift[i]);}
                for(unsigned iteration=0;iteration<ProjectionPasses;++iteration){
                    for(const auto& e:c.edges){const auto delta=Sub(c.positions[e.b],c.positions[e.a]);const double length=Length(delta),weight=c.inverseMass[e.a]+c.inverseMass[e.b];if(length<1e-12||weight==0)continue;const auto correction=Scale(delta,e.stiffness*(length-e.rest)/(length*weight));c.positions[e.a]=Add(c.positions[e.a],Scale(correction,c.inverseMass[e.a]));c.positions[e.b]=Sub(c.positions[e.b],Scale(correction,c.inverseMass[e.b]));}
                    for(const auto& bend:c.bends){std::array<Vec3,4> points;std::array<double,4> mass;
                        for(unsigned j=0;j<4;++j){points[j]=c.positions[bend.vertices[j]];mass[j]=c.inverseMass[bend.vertices[j]];}
                        double diagnosticAngle=0;if(diagnoseCollisions)ClothBendGeometry(points,diagnosticAngle);
                        if(ProjectClothBend(points,mass,bend.rest,bend.stiffness)){
                            if(diagnoseCollisions){double afterAngle;if(ClothBendGeometry(points,afterAngle)){constexpr double twoPi=6.28318530717958647692;const double increase=std::abs(std::remainder(afterAngle-bend.rest,twoPi))-std::abs(std::remainder(diagnosticAngle-bend.rest,twoPi));if(increase>1e-6){++diagnostics.bendingErrorIncreases;diagnostics.maximumBendingErrorIncrease=std::max(diagnostics.maximumBendingErrorIncrease,increase);}}}
                            for(unsigned j=0;j<4;++j){if(diagnoseCollisions){const auto correction=Length(Sub(points[j],c.positions[bend.vertices[j]]));if(correction>diagnostics.maximumBendProjection){diagnostics.maximumBendProjection=correction;diagnostics.maximumBendVertex=bend.vertices[j];diagnostics.maximumBendAngle=diagnosticAngle;diagnostics.maximumBendRestAngle=bend.rest;}}c.positions[bend.vertices[j]]=points[j];}}}
                    for(const auto& tether:c.tethers)if(c.inverseMass[tether.particle]){
                        const auto delta=Sub(c.positions[tether.particle],c.positions[tether.anchor]);const auto length=Length(delta);
                        if(length>tether.maximumLength){if(diagnoseCollisions&&length-tether.maximumLength>diagnostics.maximumTetherProjection){diagnostics.maximumTetherProjection=length-tether.maximumLength;diagnostics.maximumTetherVertex=tether.particle;}c.positions[tether.particle]=Add(c.positions[tether.anchor],Scale(delta,tether.maximumLength/length));}
                    }
                    for(size_t i=0;i<c.positions.size();++i)if(c.inverseMass[i])for(const auto& p:capsules){auto local=TransformPoint(p.inverse,c.positions[i]);const Vec3 center{0,0,std::clamp(local.z,-p.height*.5f,p.height*.5f)};const auto delta=Sub(local,center);const auto distance=Length(delta);if(distance<p.radius)local=Add(center,Scale(Unit(delta,{1,0,0}),p.radius));else continue;
                        const auto projected=TransformPoint(p.local,local);
                        if(diagnoseCollisions){++diagnostics.contacts;const auto correction=Length(Sub(projected,c.positions[i]));if(correction>diagnostics.maximumProjection){diagnostics.maximumProjection=correction;diagnostics.maximumProjectionStep=diagnostics.steps;diagnostics.maximumProjectionVertex=uint32_t(i);diagnostics.maximumProjectionBone=c.source->constraints[p.sourceIndex].bone;}}
                        c.positions[i]=projected;
                        if(iteration+1==ProjectionPasses){const auto n=Unit(delta,{1,0,0});const Vec3 normal=Unit({float(p.inverse.At(0,0)*n.x+p.inverse.At(1,0)*n.y+p.inverse.At(2,0)*n.z),float(p.inverse.At(0,1)*n.x+p.inverse.At(1,1)*n.y+p.inverse.At(2,1)*n.z),float(p.inverse.At(0,2)*n.x+p.inverse.At(1,2)*n.y+p.inverse.At(2,2)*n.z)});
                            contacts.push_back({i,normal,Scale(Sub(projected,TransformPoint(p.prior,local)),1/dt)});}
                    }
                }
                // A corrected contact has zero restitution: its final relative
                // normal velocity is zero in either direction. Subtracting a
                // sum of intermediate depenetrations can invent tangential
                // velocity when capsule and fabric constraints oppose each other.
                // Tracking is a first-order authored pose-follow rate, not an
                // additional velocity impulse. Counting its positional shift
                // again as velocity made the implicit spring grow with Hz.
                for(size_t i=0;i<c.positions.size();++i){c.velocities[i]=Scale(Sub(Sub(c.positions[i],old[i]),trackingShift[i]),1/dt);const auto displacement=Length(Sub(c.positions[i],targets[i]));if(!std::isfinite(displacement)||displacement>1000)throw Error("Cloth simulation exceeded finite displacement bounds");}
                for(const auto& contact:contacts){auto& velocity=c.velocities[contact.particle];velocity=ConstrainClothContactVelocity(velocity,contact.normal,contact.velocity);}
                if(diagnoseCollisions){
                    ++diagnostics.steps;Pose queryPose;queryPose.world.push_back(Matrix::Identity());
                    for(const auto& capsule:capsules){
                        Matrix priorInverse;try{priorInverse=InverseAffine(capsule.prior);}catch(const Error&){continue;}
                        CollisionData query;query.tight.kind=2;query.tight.bone=0;query.tight.size={capsule.radius,capsule.height,0};
                        const double clearance=64*std::numeric_limits<float>::epsilon()*std::max(1.f,capsule.radius),outsideRadius=capsule.radius+clearance;
                        auto outside=[&](Vec3 p){const auto z=std::clamp(p.z,-capsule.height*.5f,capsule.height*.5f);return double(p.x)*p.x+double(p.y)*p.y+double(p.z-z)*(p.z-z)>outsideRadius*outsideRadius;};
                        for(size_t i=0;i<c.positions.size();++i)if(c.inverseMass[i]){
                            const auto a=TransformPoint(priorInverse,old[i]),b=TransformPoint(capsule.inverse,c.positions[i]);
                            if(!outside(a)||!outside(b))continue;const auto direction=Sub(b,a);if(Dot(direction,direction)<1e-20)continue;
                            ++diagnostics.sweeps;double fraction;if(IntersectCollision(query,queryPose,{a,direction,1},fraction))++diagnostics.outsideCrossings;
                        }
                    }
                }
            }
            for(size_t i=0;i<c.positions.size();++i){if(!c.inverseMass[i])c.positions[i]=targets[i];maximumDisplacement=std::max(maximumDisplacement,Length(Sub(c.positions[i],targets[i])));}
            auto& simulation=RegionAt(frame,c.source->simulationRegion);for(size_t i=0;i<c.positions.size();++i)simulation.vertices.at(i).position=c.positions[i];RebuildBasis(simulation);
            for(const auto& map:c.source->influences){auto& output=RegionAt(frame,map.influencedRegion);for(size_t i=0;i<output.vertices.size();++i){Vec3 delta{};unsigned sum=0;for(unsigned k=0;k<4;++k){const auto w=(map.weights[i]>>(k*8))&255u;if(w){const auto index=(map.lookups[i]>>(k*16))&65535u;delta=Add(delta,Scale(Sub(c.positions[index],targets[index]),w));sum+=w;}}if(sum)output.vertices[i].position=Add(output.vertices[i].position,Scale(delta,1.0/sum));}RebuildBasis(output);}
            c.previousTargets=targets;c.previousCapsules=currentCapsules;
        }
        steps+=count;
    }
};
ClothSimulation::ClothSimulation(const Scene& scene):m_state(std::make_unique<State>(scene)){}
ClothSimulation::~ClothSimulation()=default;
void ClothSimulation::Reset(){m_state->accumulator=0;m_state->maximumDisplacement=0;for(auto& c:m_state->cloths)c.initialized=false;++m_state->resets;}
void ClothSimulation::Rebase(const SceneFrame& frame){m_state->Rebase(frame);}
void ClothSimulation::SetWorldTransform(const Matrix& world){
    const auto inverse=InverseAffine(world);
    if(m_state->hasWorld){
        const auto delta=Multiply(inverse,m_state->world);
        const auto origin=TransformPoint(delta,{});
        bool reset=Length(origin)>32;
        // Changes to object scale invalidate the source-local rest lengths.
        for(unsigned a=0;a<3;++a)for(unsigned b=0;b<3;++b){double dot=0;for(unsigned k=0;k<3;++k)dot+=delta.At(k,a)*delta.At(k,b);if(std::abs(dot-(a==b?1.0:0.0))>1e-3)reset=true;}
        if(reset)Reset();else for(auto& cloth:m_state->cloths)if(cloth.initialized){
            for(auto& p:cloth.positions)p=TransformPoint(delta,p);
            for(auto& p:cloth.previousTargets)p=TransformPoint(delta,p);
            for(auto& p:cloth.previousCapsules)p=Multiply(delta,p);
            for(auto& p:cloth.tickCapsules)p=Multiply(delta,p);
            for(auto& v:cloth.velocities)v=Sub(TransformPoint(delta,v),origin);
        }
    }
    m_state->world=world;m_state->hasWorld=true;
}
void ClothSimulation::Advance(SceneFrame& frame,double seconds){m_state->Advance(frame,seconds);}
void ClothSimulation::SetAir(double density,Vec3 wind){if(!std::isfinite(density)||density<0||!std::isfinite(wind.x)||!std::isfinite(wind.y)||!std::isfinite(wind.z))throw Error("Invalid cloth air environment");m_state->fluidDensity=density;m_state->worldWind=wind;}
size_t ClothSimulation::Cloths()const{return m_state->cloths.size();}
size_t ClothSimulation::Particles()const{return m_state->particles;}
size_t ClothSimulation::MappedVertices()const{return m_state->mapped;}
size_t ClothSimulation::BendingConstraints()const{size_t count=0;for(const auto& c:m_state->cloths)count+=c.bends.size();return count;}
size_t ClothSimulation::Tethers()const{size_t count=0;for(const auto& c:m_state->cloths)count+=c.tethers.size();return count;}
double ClothSimulation::FrequencyHz()const{return SolverHz;}
uint32_t ClothSimulation::ProjectionIterations()const{return ProjectionPasses;}
void ClothSimulation::EnableCollisionDiagnostics(bool enabled){m_state->diagnoseCollisions=enabled;m_state->diagnostics={};}
ClothCollisionDiagnostics ClothSimulation::CollisionDiagnostics()const{return m_state->diagnostics;}
uint64_t ClothSimulation::Steps()const{return m_state->steps;}
uint64_t ClothSimulation::Resets()const{return m_state->resets;}
double ClothSimulation::MaximumDisplacement()const{return m_state->maximumDisplacement;}
}
