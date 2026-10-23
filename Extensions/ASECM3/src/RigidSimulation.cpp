#include "ASECM3/RigidSimulation.h"
#include "ASECM3/JointResponse.h"
#include "ASECM3/RigidMassProperties.h"
#include "ASECM3/ConvexPhysics.h"
#include "btBulletDynamicsCommon.h"
#include "BulletDynamics/ConstraintSolver/btFixedConstraint.h"
#include "LinearMath/btTransformUtil.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
namespace ASECM3 {
namespace {
class ConvexCollision final:public btConvexHullShape {
    btVector3 unitInertia;
public:
    explicit ConvexCollision(const ConvexPhysicsMesh& mesh):unitInertia(mesh.inertiaPerMass.x,mesh.inertiaPerMass.y,mesh.inertiaPerMass.z){
        for(const auto& vertex:mesh.vertices)addPoint(btVector3(vertex.x,vertex.y,vertex.z),false);
        setMargin(.005f);recalcLocalAabb();
    }
    void calculateLocalInertia(btScalar mass,btVector3& inertia)const override{inertia=unitInertia*mass;}
};
struct Affine {btTransform rigid;btVector3 scale;};
Affine Split(const Matrix& m){
    btVector3 cols[3];btScalar scales[3];
    for(unsigned c=0;c<3;++c){cols[c]=btVector3(btScalar(m.At(0,c)),btScalar(m.At(1,c)),btScalar(m.At(2,c)));scales[c]=cols[c].length();if(scales[c]<1e-6f)throw Error("Rigid preview cannot bind a zero-scale bone/shape");cols[c]/=scales[c];}
    if(btFabs(cols[0].dot(cols[1]))>1e-3f||btFabs(cols[0].dot(cols[2]))>1e-3f||btFabs(cols[1].dot(cols[2]))>1e-3f||cols[0].cross(cols[1]).dot(cols[2])<.999f)throw Error("Rigid preview does not support sheared or reflected frames");
    btMatrix3x3 rotation;for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)rotation[r][c]=cols[c][r];
    return {btTransform(rotation,btVector3(btScalar(m.At(0,3)),btScalar(m.At(1,3)),btScalar(m.At(2,3)))),btVector3(scales[0],scales[1],scales[2])};
}
Matrix MatrixOf(const btTransform& t,const btVector3& scale){Matrix out=Matrix::Identity();for(unsigned r=0;r<3;++r){for(unsigned c=0;c<3;++c)out.At(r,c)=double(t.getBasis()[r][c])*scale[c];out.At(r,3)=t.getOrigin()[r];}return out;}
Matrix BindInverse(const Bone& bone){Matrix m;std::copy_n(bone.inverseBind,16,m.values);return m;}
double Separation(const btTransform& a,const btTransform& b){return double((a.getOrigin()-b.getOrigin()).length());}
}
struct RigidSimulation::State {
    const Scene& scene;
    btDefaultCollisionConfiguration configuration;
    btCollisionDispatcher dispatcher{&configuration};btDbvtBroadphase broadphase;
    btSequentialImpulseConstraintSolver solver;
    btDiscreteDynamicsWorld world{&dispatcher,&broadphase,&solver,&configuration};
    // Declaration/destruction order keeps constraints/bodies ahead of shapes.
    std::vector<std::unique_ptr<btCollisionShape>> shapes;
    std::vector<std::unique_ptr<btRigidBody>> bodies;
    std::vector<std::unique_ptr<btTypedConstraint>> joints;
    struct WeldSpring {btGeneric6DofSpring2Constraint* joint;double frequency,ratio;};
    std::vector<WeldSpring> weldSprings;
    std::vector<uint32_t> bones;
    std::vector<bool> dynamic;
    Pose activationPose;
    std::vector<btVector3> scales;
    std::vector<btTransform> initial;
    std::vector<btTransform> centers;
    std::vector<std::pair<size_t,size_t>> jointBodies;
    std::vector<std::pair<btTransform,btTransform>> jointFrames;
    double accumulator=0,maximumDisplacement=0,maximumJointError=0;uint64_t steps=0;
    bool initialized=false;
    size_t groundContacts=0;
    explicit State(const Scene& s):scene(s){
      try {
        if(!s.Physics().error.empty())throw Error(s.Physics().error);
        if(s.Physics().bodies.empty())return;
        const auto& model=s.Model();const auto pose=s.EvaluateGroup().pose;
        world.setGravity(btVector3(0,0,-9.81f));world.getSolverInfo().m_numIterations=30;
        world.getSolverInfo().m_solverMode&=~SOLVER_RANDMIZE_ORDER;
        std::unordered_map<uint32_t,size_t> byBone;
        bodies.reserve(s.Physics().bodies.size()+1);bones.reserve(s.Physics().bodies.size());scales.reserve(s.Physics().bodies.size());initial.reserve(s.Physics().bodies.size());
        joints.reserve(s.Physics().joints.size());jointBodies.reserve(s.Physics().joints.size());jointFrames.reserve(s.Physics().joints.size());
        size_t shapeCount=s.Physics().bodies.size()+1;for(const auto& body:s.Physics().bodies)shapeCount+=body.shapes.size();shapes.reserve(shapeCount);
        // Conservative preview policy: source collision bodies are not all ragdoll
        // bodies. Only a joint-connected skeleton chain may move dynamically.
        // A chain missing its link to the next ancestor stays animation anchored.
        std::unordered_map<uint32_t,size_t> sourceBones;
        const auto& sources=s.Physics().bodies;
        std::vector<size_t> components(sources.size());
        for(size_t i=0;i<sources.size();++i){sourceBones[sources[i].bone]=i;components[i]=i;}
        auto component=[&](size_t i){while(components[i]!=i)i=components[i];return i;};
        std::vector<unsigned> degree(sources.size(),0);
        for(const auto& joint:s.Physics().joints){
            const auto a=sourceBones.find(joint.endpoint1),b=sourceBones.find(joint.endpoint2);
            if(a==sourceBones.end()||b==sourceBones.end())throw Error("Rigid joint has no source body");
            components[component(a->second)]=component(b->second);++degree[a->second];++degree[b->second];
        }
        for(size_t i=0;i<sources.size();++i){
            int32_t parent=model.skeleton.at(sources[i].bone).parent;
            while(parent>=0&&!sourceBones.count(uint32_t(parent)))parent=model.skeleton.at(parent).parent;
            const bool connected=parent>=0&&component(i)==component(sourceBones.at(uint32_t(parent)));
            const bool jointedRoot=model.skeleton.at(sources[i].bone).parent<0&&degree[i]>0;
            dynamic.push_back((connected||jointedRoot)&&!(sources[i].flags&0x80));
        }
        for(const auto& source:s.Physics().bodies){
            if(source.version<3)throw Error("Legacy PHRB v2 needs a separate material/state adapter");
            if(!byBone.emplace(source.bone,bones.size()).second)throw Error("Multiple rigid bodies share a bone");
            const auto boneFrame=Split(pose.world.at(source.bone));
            auto compound=std::make_unique<btCompoundShape>();double volume=0;std::vector<double> childMasses;
            for(const auto& shape:source.shapes){
                const double previousVolume=volume;
                const auto local=shape.kind==4?Affine{btTransform::getIdentity(),btVector3(1,1,1)}:Split(shape.local);const auto scale=local.scale*boneFrame.scale;
                if(shape.kind==1&&(btFabs(scale.x()-scale.y())>1e-4f||btFabs(scale.x()-scale.z())>1e-4f))throw Error("Nonuniform rigid sphere scaling requires an ellipsoid adapter");
                if(shape.kind==2&&btFabs(scale.x()-scale.y())>1e-4f)throw Error("Noncircular rigid capsule scaling is unsupported");
                const float a=shape.size[0],b=shape.size[1],c=shape.size[2];
                if(shape.kind<=3&&(a<=0||((shape.kind==0||shape.kind==2||shape.kind==3)&&b<0)|| (shape.kind==0&&(b<=0||c<=0))))throw Error("Invalid rigid collision dimensions");
                std::unique_ptr<btCollisionShape> collision;
                auto transform=local.rigid;
                constexpr double pi=3.141592653589793;
                // m3studio's cube is [-1,+1] scaled by each serialized size:
                // PHSH box values are half extents, unlike capsule height.
                if(shape.kind==0){collision=std::make_unique<btBoxShape>(btVector3(a,b,c));volume+=8*a*b*c*scale.x()*scale.y()*scale.z();}
                else if(shape.kind==1){collision=std::make_unique<btSphereShape>(a);volume+=4*pi*a*a*a/3*scale.x()*scale.y()*scale.z();}
                else if(shape.kind==2){collision=std::make_unique<btCapsuleShapeZ>(a,b);volume+=(pi*a*a*b+4*pi*a*a*a/3)*scale.x()*scale.y()*scale.z();}
                else if(shape.kind==3){collision=std::make_unique<btCylinderShapeZ>(btVector3(a,a,b*.5f));volume+=pi*a*a*b*scale.x()*scale.y()*scale.z();}
                else if(shape.kind==4){
                    const auto mesh=BuildConvexPhysics(shape,{boneFrame.scale.x(),boneFrame.scale.y(),boneFrame.scale.z()});
                    collision=std::make_unique<ConvexCollision>(mesh);volume+=mesh.volume;transform=Split(mesh.childFrame).rigid;
                }
                else throw Error("Rigid collision mesh requires verified PHSH topology; no primitive substitute used");
                if(shape.kind!=4){collision->setLocalScaling(scale);collision->setMargin(.005f);transform.setOrigin(transform.getOrigin()*boneFrame.scale);}
                compound->addChildShape(transform,collision.get());shapes.push_back(std::move(collision));
                childMasses.push_back(double(source.mass)*(volume-previousVolume));
            }
            if(!compound->getNumChildShapes())throw Error("Rigid body has no collision shape");
            // The serialized source 'mass' field is density in Art Tools.
            const double mass=double(source.mass)*volume;
            if(!std::isfinite(mass)||mass<=0||mass>1e9)throw Error("Rigid body density/volume produces invalid mass");
            const auto properties=RecenterRigidCompound(*compound,childMasses);
            const auto center=Split(properties.centerInBone).rigid;
            const bool isDynamic=dynamic[bones.size()];
            const btVector3 inertia=isDynamic?btVector3(properties.inertia.x,properties.inertia.y,properties.inertia.z):btVector3(0,0,0);
            btRigidBody::btRigidBodyConstructionInfo ci(isDynamic?btScalar(mass):0,nullptr,compound.get(),inertia);
            ci.m_startWorldTransform=boneFrame.rigid*center;ci.m_friction=std::clamp(source.friction,0.0f,1.0f);ci.m_restitution=std::clamp(source.bounce,0.0f,1.0f);
            auto body=std::make_unique<btRigidBody>(ci);body->setDamping(std::clamp(source.linearDamping,0.0f,1.0f),std::clamp(source.angularDamping,0.0f,1.0f));
            if(!isDynamic)body->setCollisionFlags(body->getCollisionFlags()|btCollisionObject::CF_KINEMATIC_OBJECT);
            body->setActivationState(DISABLE_DEACTIVATION);body->setFlags(body->getFlags()|BT_DISABLE_WORLD_GRAVITY);body->setGravity(btVector3(0,0,-9.81f*source.gravityFactor));
            // Preview ground group 1, body group 2. Art Tools stackable controls
            // body-body interaction; connected pairs are also filtered below.
            const int mask=(source.flags&1)?(1|((source.flags&4)&&!(source.flags&16)?2:0)):0;
            shapes.push_back(std::move(compound));world.addRigidBody(body.get(),2,mask);
            bones.push_back(source.bone);scales.push_back(boneFrame.scale);initial.push_back(boneFrame.rigid);bodies.push_back(std::move(body));
            centers.push_back(center);
        }
        for(const auto& source:s.Physics().joints){
            const auto a=byBone.find(source.endpoint1),b=byBone.find(source.endpoint2);
            if(a==byBone.end()||b==byBone.end()||a->second==b->second)throw Error("Rigid joint endpoint does not resolve to distinct body bones");
            const auto af=Split(source.frame1),bf=Split(source.frame2);auto fa=af.rigid,fb=bf.rigid;
            fa.setOrigin(fa.getOrigin()*scales[a->second]);fb.setOrigin(fb.getOrigin()*scales[b->second]);
            // Source frames are bone-local. Check actual authored pivot
            // agreement rather than silently snapping mismatched constraints.
            const auto error=Separation(initial[a->second]*fa,initial[b->second]*fb);
            if(error>.02)throw Error("Rigid source joint pivots disagree in bind space: "+std::to_string(error));
            fa=centers[a->second].inverse()*fa;fb=centers[b->second].inverse()*fb;
            if(!dynamic[a->second]&&!dynamic[b->second])continue;
            auto& ba=*bodies[a->second];auto& bb=*bodies[b->second];std::unique_ptr<btTypedConstraint> joint;
            if(source.kind==0)joint=std::make_unique<btPoint2PointConstraint>(ba,bb,fa.getOrigin(),fb.getOrigin());
            else if(source.kind==1){auto hinge=std::make_unique<btHingeConstraint>(ba,bb,fa,fb);if(source.limit)hinge->setLimit(source.minimum,source.maximum);joint=std::move(hinge);}
            else if(source.kind==2){
                // Source cone axis is Z; Bullet cone twist axis is X. Rotate
                // both local bases by the same proper cyclic permutation.
                const btMatrix3x3 axis(0,1,0, 0,0,1, 1,0,0);fa.setBasis(fa.getBasis()*axis);fb.setBasis(fb.getBasis()*axis);
                auto cone=std::make_unique<btConeTwistConstraint>(ba,bb,fa,fb);if(source.limit)cone->setLimit(source.angle,source.angle,std::max(btFabs(source.minimum),btFabs(source.maximum)));joint=std::move(cone);
            }else if(source.kind==3){
                if(source.angularFrequency<0||source.dampingRatio<0)throw Error("Negative weld spring frequency/damping ratio");
                if(source.angularFrequency==0)joint=std::make_unique<btFixedConstraint>(ba,bb,fa,fb);
                else{
                    auto spring=std::make_unique<btGeneric6DofSpring2Constraint>(ba,bb,fa,fb);
                    spring->setLinearLowerLimit(btVector3(0,0,0));spring->setLinearUpperLimit(btVector3(0,0,0));
                    spring->setAngularLowerLimit(btVector3(1,1,1));spring->setAngularUpperLimit(btVector3(-1,-1,-1));
                    for(unsigned axis=0;axis<3;++axis)spring->enableSpring(axis+3,true);
                    spring->setEquilibriumPoint();
                    weldSprings.push_back({spring.get(),source.angularFrequency,source.dampingRatio});joint=std::move(spring);
                }
            }
            else throw Error("Unsupported source physics joint type");
            world.addConstraint(joint.get(),true);jointBodies.push_back({a->second,b->second});jointFrames.push_back({fa,fb});joints.push_back(std::move(joint));
        }
        auto plane=std::make_unique<btStaticPlaneShape>(btVector3(0,0,1),btScalar(model.lower.z));
        btRigidBody::btRigidBodyConstructionInfo ci(0,nullptr,plane.get());auto ground=std::make_unique<btRigidBody>(ci);ground->setFriction(.7f);
        world.addRigidBody(ground.get(),1,2);shapes.push_back(std::move(plane));bodies.push_back(std::move(ground));
        UpdateSprings();
      }catch(...){Detach();throw;}
    }
    void Detach(){for(auto& j:joints)world.removeConstraint(j.get());for(auto& b:bodies)world.removeRigidBody(b.get());}
    ~State(){Detach();}
    void UpdateSprings(){
        for(auto& weld:weldSprings){
            auto& joint=*weld.joint;joint.calculateTransforms();
            for(unsigned axis=0;axis<3;++axis){
                const auto direction=joint.getAxis(axis).normalized();
                const auto inverseInertia=direction.dot(joint.getRigidBodyA().getInvInertiaTensorWorld()*direction)+direction.dot(joint.getRigidBodyB().getInvInertiaTensorWorld()*direction);
                const auto response=AngularSpring(weld.frequency,weld.ratio,double(inverseInertia));
                // Bullet limits unstable coefficients for the fixed step. The
                // requested values retain the source rad/s and ratio semantics.
                joint.setStiffness(axis+3,btScalar(response.stiffness),true);
                joint.setDamping(axis+3,btScalar(response.damping),true);
            }
        }
    }
    void Seed(const SceneFrame& frame){activationPose=frame.pose;for(size_t i=0;i<bones.size();++i){const auto pose=Split(frame.pose.world.at(bones[i]));if((pose.scale-scales[i]).length()>1e-3f)throw Error("Animated rigid-body scale differs from solver bind scale");const auto centerPose=pose.rigid*centers[i];bodies[i]->setWorldTransform(centerPose);bodies[i]->setInterpolationWorldTransform(centerPose);bodies[i]->setLinearVelocity(btVector3(0,0,0));bodies[i]->setAngularVelocity(btVector3(0,0,0));bodies[i]->clearForces();world.updateSingleAabb(bodies[i].get());if(bodies[i]->getBroadphaseHandle())world.getBroadphase()->getOverlappingPairCache()->cleanProxyFromPairs(bodies[i]->getBroadphaseHandle(),&dispatcher);}initialized=true;}
    void Advance(SceneFrame& frame,double seconds){
        if(!std::isfinite(seconds)||seconds<0)throw Error("Invalid rigid frame interval");
        if(bones.empty())return;if(seconds>.25){initialized=false;accumulator=0;seconds=0;}
        if(!initialized)Seed(frame);accumulator+=seconds;constexpr double dt=1.0/120;const unsigned count=unsigned(std::min(30.0,std::floor((accumulator+1e-12)/dt)));accumulator-=count*dt;
        for(unsigned i=0;i<count;++i){
            for(size_t b=0;b<bones.size();++b)if(!dynamic[b]){
                const auto target=Split(frame.pose.world.at(bones[b]));
                if((target.scale-scales[b]).length()>1e-3f)throw Error("Animated kinematic scale differs from solver bind scale");
                const auto targetCenter=target.rigid*centers[b];const auto from=bodies[b]->getWorldTransform();const auto blend=btScalar(1.0/double(count-i));
                const btTransform next(from.getRotation().slerp(targetCenter.getRotation(),blend),from.getOrigin().lerp(targetCenter.getOrigin(),blend));
                bodies[b]->setInterpolationWorldTransform(from);bodies[b]->setWorldTransform(next);
                btVector3 linear,angular;btTransformUtil::calculateVelocity(from,next,btScalar(dt),linear,angular);
                bodies[b]->setLinearVelocity(linear);bodies[b]->setAngularVelocity(angular);world.updateSingleAabb(bodies[b].get());
            }
            UpdateSprings();world.stepSimulation(btScalar(dt),0,btScalar(dt));++steps;}
        const auto original=frame.pose;std::unordered_map<uint32_t,size_t> direct;for(size_t i=0;i<bones.size();++i)direct[bones[i]]=i;
        maximumDisplacement=0;
        for(size_t bone=0;bone<frame.pose.world.size();++bone){int32_t ancestor=int32_t(bone);while(ancestor>=0&&!direct.count(uint32_t(ancestor)))ancestor=scene.Model().skeleton[ancestor].parent;
            if(ancestor<0)continue;const auto index=direct.at(uint32_t(ancestor));if(!dynamic[index])continue;const auto solved=MatrixOf(bodies[index]->getWorldTransform()*centers[index].inverse(),scales[index]);
            const auto delta=Multiply(solved,InverseAffine(activationPose.world.at(ancestor)));frame.pose.world[bone]=Multiply(delta,activationPose.world[bone]);frame.pose.skin[bone]=Multiply(frame.pose.world[bone],BindInverse(scene.Model().skeleton[bone]));
            const auto inverse=InverseAffine(frame.pose.skin[bone]);frame.pose.normal[bone]=Matrix{};for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)frame.pose.normal[bone].At(r,c)=inverse.At(c,r);
            const Vec3 before{float(original.world[bone].At(0,3)),float(original.world[bone].At(1,3)),float(original.world[bone].At(2,3))},after{float(frame.pose.world[bone].At(0,3)),float(frame.pose.world[bone].At(1,3)),float(frame.pose.world[bone].At(2,3))};const double distance=std::sqrt(double(after.x-before.x)*(after.x-before.x)+double(after.y-before.y)*(after.y-before.y)+double(after.z-before.z)*(after.z-before.z));if(!std::isfinite(distance)||distance>1000)throw Error("Rigid pose exceeded finite displacement bounds");maximumDisplacement=std::max(maximumDisplacement,distance);
        }
        const auto skinned=SkinModel(scene.Model(),frame.pose);for(const auto& skin:skinned)if(skin.division==0)for(auto& region:frame.regions)if(region.region==skin.region){for(size_t i=0;i<region.vertices.size();++i){region.vertices[i].position=skin.positions.at(i);region.vertices[i].normal=skin.normals.at(i);region.vertices[i].tangent=skin.tangents.at(i);region.vertices[i].tangentSign=skin.tangentSigns.at(i);}}
        maximumJointError=0;for(size_t i=0;i<jointBodies.size();++i){const auto pair=jointBodies[i];maximumJointError=std::max(maximumJointError,Separation(bodies[pair.first]->getWorldTransform()*jointFrames[i].first,bodies[pair.second]->getWorldTransform()*jointFrames[i].second));}
        groundContacts=0;for(int i=0;i<dispatcher.getNumManifolds();++i){const auto* contact=dispatcher.getManifoldByIndexInternal(i);if(contact->getBody0()!=bodies.back().get()&&contact->getBody1()!=bodies.back().get())continue;for(int j=0;j<contact->getNumContacts();++j)if(contact->getContactPoint(j).getDistance()<=.01f)++groundContacts;}
    }
};
RigidSimulation::RigidSimulation(const Scene& s):m_state(std::make_unique<State>(s)){}
RigidSimulation::~RigidSimulation()=default;
void RigidSimulation::Reset(){m_state->initialized=false;m_state->accumulator=0;m_state->maximumDisplacement=0;}
void RigidSimulation::Activate(const SceneFrame& frame,const SceneFrame* previous,double interval){
    Reset();m_state->Seed(frame);if(!previous)return;
    if(!std::isfinite(interval)||interval<=0||interval>.25)throw Error("Invalid rigid activation velocity interval");
    for(size_t i=0;i<m_state->bones.size();++i){if(!m_state->dynamic[i])continue;const auto a=Split(previous->pose.world.at(m_state->bones[i])),b=Split(frame.pose.world.at(m_state->bones[i]));btVector3 linear,angular;btTransformUtil::calculateVelocity(a.rigid*m_state->centers[i],b.rigid*m_state->centers[i],btScalar(interval),linear,angular);if(!std::isfinite(double(linear.length2()))||!std::isfinite(double(angular.length2()))||linear.length()>1000||angular.length()>1000)throw Error("Rigid activation velocity exceeds finite limits");m_state->bodies[i]->setLinearVelocity(linear);m_state->bodies[i]->setAngularVelocity(angular);}
}
void RigidSimulation::Advance(SceneFrame& frame,double seconds){m_state->Advance(frame,seconds);}
size_t RigidSimulation::Bodies()const{return m_state->bones.size();}
size_t RigidSimulation::MassFrames()const{return m_state->centers.size();}
size_t RigidSimulation::DynamicBodies()const{return std::count(m_state->dynamic.begin(),m_state->dynamic.end(),true);}
size_t RigidSimulation::Joints()const{return m_state->joints.size();}
uint64_t RigidSimulation::Steps()const{return m_state->steps;}
double RigidSimulation::MaximumDisplacement()const{return m_state->maximumDisplacement;}
double RigidSimulation::MaximumJointError()const{return m_state->maximumJointError;}
size_t RigidSimulation::GroundContacts()const{return m_state->groundContacts;}
size_t RigidSimulation::SpringJoints()const{return m_state->weldSprings.size();}
}
