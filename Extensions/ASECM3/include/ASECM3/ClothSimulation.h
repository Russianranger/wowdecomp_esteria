#pragma once
#include "Scene.h"
namespace ASECM3 {
struct ClothCollisionDiagnostics {
    uint64_t steps=0,contacts=0,outsideCrossings=0,sweeps=0;
    double maximumProjection=0;
    uint64_t maximumProjectionStep=0;
    uint32_t maximumProjectionVertex=UINT32_MAX;
    int32_t maximumProjectionBone=-1;
    double maximumBendProjection=0,maximumTetherProjection=0;
    uint32_t maximumBendVertex=UINT32_MAX,maximumTetherVertex=UINT32_MAX;
    double maximumBendAngle=0,maximumBendRestAngle=0;
    uint64_t bendingErrorIncreases=0;
    double maximumBendingErrorIncrease=0;
};
// An instance owns its clock/particles. Input Scene must outlive the instance.
// This implements preview cloth dynamics, not the proprietary Domino solver.
class ClothSimulation final {
    struct State;
    std::unique_ptr<State> m_state;
public:
    explicit ClothSimulation(const Scene& scene);
    ~ClothSimulation();
    void Reset();
    // An explicit clip change transports the existing drape to the new skin
    // targets without treating the pose discontinuity as a physical impulse.
    void Rebase(const SceneFrame& frame);
    // Move existing particles into the current object's coordinate frame while
    // retaining their world-space inertia. First call establishes the frame.
    void SetWorldTransform(const Matrix& sourceToWorld);
    // Explicit environment, in source-consistent length/mass/time units.
    // Density defaults to zero until host/source unit calibration is known.
    // World wind is transformed without translation; PHCL local wind is added.
    void SetAir(double fluidDensity,Vec3 worldWindVelocity={});
    void Advance(SceneFrame& frame,double seconds);
    size_t Cloths() const;
    size_t Particles() const;
    size_t MappedVertices() const;
    size_t BendingConstraints() const;
    size_t Tethers() const;
    double FrequencyHz() const;
    uint32_t ProjectionIterations() const;
    // Optional offline diagnostic sampling. Disabled by default; it does not
    // alter particle/contact response and adds no sweep queries when disabled.
    void EnableCollisionDiagnostics(bool enabled);
    ClothCollisionDiagnostics CollisionDiagnostics() const;
    uint64_t Steps() const;
    uint64_t Resets() const;
    double MaximumDisplacement() const;
};
}
