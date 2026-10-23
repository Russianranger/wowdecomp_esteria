#pragma once
namespace ASECM3 {
struct AngularSpringResponse {
    double effectiveInertia=0,stiffness=0,damping=0;
};
// omega is an angular frequency in rad/s, not cycles/s. The dimensionless
// damping ratio may exceed one (overdamped source joints are valid).
AngularSpringResponse AngularSpring(double omega,double dampingRatio,double inverseEffectiveInertia);
}
