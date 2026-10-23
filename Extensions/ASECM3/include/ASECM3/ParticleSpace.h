#pragma once
#include "ParticleEmitter.h"
namespace ASECM3 {
// Billboards carry one scalar size. Explicitly reject skew/nonuniform scale
// rather than silently choosing an axis or stretching camera-facing quads.
double ParticleUniformScale(const Matrix& transform);
std::vector<ParticleState> CameraParticles(const std::vector<ParticleState>& worldParticles,const Matrix& worldToCamera);
bool ParticleSourceBounds(const std::vector<ParticleState>& worldParticles,const Matrix& sourceToWorld,Vec3& lower,Vec3& upper);
}
