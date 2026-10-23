#include "ASECM3/JointResponse.h"
#include "ASECM3/Document.h"
#include <cmath>
namespace ASECM3 {
AngularSpringResponse AngularSpring(double omega,double dampingRatio,double inverseEffectiveInertia){
    if(!std::isfinite(omega)||!std::isfinite(dampingRatio)||!std::isfinite(inverseEffectiveInertia)||omega<0||omega>1000||dampingRatio<0||dampingRatio>1000||inverseEffectiveInertia<=1e-12)throw Error("Invalid angular spring frequency, damping ratio or effective inertia");
    AngularSpringResponse out;out.effectiveInertia=1/inverseEffectiveInertia;
    out.stiffness=out.effectiveInertia*omega*omega;
    out.damping=2*dampingRatio*out.effectiveInertia*omega;
    if(!std::isfinite(out.stiffness)||!std::isfinite(out.damping))throw Error("Angular spring coefficients exceed finite limits");
    return out;
}
}
