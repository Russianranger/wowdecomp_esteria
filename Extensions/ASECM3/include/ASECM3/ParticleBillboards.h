#pragma once
#include "ParticleEmitter.h"
namespace ASECM3 {
SceneRegion BuildParticleBillboards(const std::vector<ParticleState>& particles,Vec3 cameraRight,Vec3 cameraUp);
// A private particle-pass adaptation, never a mutation of Scene materials:
// particle UV source6 consumes generated quad UV0, per-particle RGBA is enabled.
Material ParticleSurfaceMaterial(const Material& source);
}
