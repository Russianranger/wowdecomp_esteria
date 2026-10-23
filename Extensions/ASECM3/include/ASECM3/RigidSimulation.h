#pragma once
#include "Scene.h"
namespace ASECM3 {
// Explicit ragdoll preview mode. Automatic source activation tracks/events
// remain a separate policy; enabling this world never modifies the Scene.
class RigidSimulation final {
    struct State;
    std::unique_ptr<State> m_state;
public:
    explicit RigidSimulation(const Scene& scene);
    ~RigidSimulation();
    void Reset();
    void Activate(const SceneFrame& frame,const SceneFrame* previous=nullptr,double interval=0);
    void Advance(SceneFrame& frame,double seconds);
    size_t Bodies() const;
    size_t DynamicBodies() const;
    size_t MassFrames() const;
    size_t Joints() const;
    uint64_t Steps() const;
    double MaximumDisplacement() const;
    double MaximumJointError() const;
    size_t GroundContacts() const;
    size_t SpringJoints() const;
};
}
