#pragma once
#include "Document.h"
#include <array>
namespace ASECM3 {
// Total triangle impulse, distributed equally over its three vertices.
// Density must use the same length/mass units as positions and inverseMass.
// This is an adapter aerodynamic model, not recovered Domino behavior.
Vec3 ClothAirImpulse(const std::array<Vec3,3>& positions,
    const std::array<Vec3,3>& velocities,const std::array<double,3>& inverseMass,
    Vec3 airVelocity,double fluidDensity,double drag,double lift,double seconds);
}
