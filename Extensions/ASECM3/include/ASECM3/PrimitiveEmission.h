#pragma once
#include "EmissionSurface.h"
#include "MaterialEvaluation.h"
namespace ASECM3 {
// Source shape 0 point, 2 sphere and 5 disc; direction 1 radial or 3 random.
// Other source enum values remain explicit unsupported cases.
void ValidatePrimitiveEmission(const EffectDefinition& emitter);
bool SamplePrimitiveEmission(const EffectDefinition& emitter,const MaterialEvaluator& values,
                             const Matrix& bone,uint32_t& random,EmissionPoint& output);
}
