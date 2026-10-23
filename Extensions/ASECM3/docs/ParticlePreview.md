# Native M3 particle preview

This records the C821 preview baseline. Newer world-rendering releases and
their verification limits are in [WorldParticles.md](WorldParticles.md).

## Current implementation

The portable core now retains birth-sampled particle appearance in its own
ParticleAppearance object. Size and rotation use the source three lifespan
values and respective midpoints. Color uses source initial/mid/end RGBA values,
with separate RGB and alpha midpoints. Source random-enable fields select the
alternate size/rotation/color controls. Appearance uses a separate serial-seeded
random stream, preserving the previously checked motion/lifetime stream.

Constant source appearance values are animated over normalized particle age.
Linear and cubic smooth interpolation are implemented; cubic smooth and uniform
random-between-endpoints are adapter policies, not original-engine fidelity
proof. Bezier, hold modes and nonzero holds report unsupported behavior rather
than silently changing controls. Source vertex-alpha control is honored.

Texture-atlas columns/rows and early/late index ranges produce per-particle UV
rectangles; zero row/column counts mean no explicit subdivision. The early/late
lifespan split and optional random starting index are explicit adapter policy
pending source comparison. Both supplied emitters use index0 throughout their
authored ranges; this data does not verify multi-frame flipbook progression.

BuildParticleBillboards is a separate portable core object. It orthogonalizes
the supplied camera right/up basis, builds rotated square quads with source
size as full side length, source RGBA bytes, per-particle atlas UVs and a valid
tangent basis. It skips zero-size/transparent particles, checks finite values
and bounds, and limits one batch to4096particles (16384vertices/24576indices).

ParticleSurfaceMaterial copies a standard source material for the private
particle pass. It maps source particle UV channel6 to generated quad UV0,
enables per-particle RGBA and two-sided quads. It never mutates Scene materials
or applies an auxiliary bank's material IDs to the body. Particle material
properties/layers currently use mesh defaults; effect-bank material ownership
and animation are still incomplete.

## Native path and control

GxParticles.cpp is a separate native adapter object. Preview-only particle
quads draw after the body through a separate GxSurface instance, inside the
existing preview GxRenderGuard. It reuses existing DDS loading, layer shaders,
source material blend mode and surface state restoration. Body and particle
surface diagnostics/counters are separate; shader restoration failures include
both paths. Missing progressive textures are retried. A particle surface error
latches only the particle pass and logs once with bank context.

Model Viewer has an Effects button in the horizontal toolbar layout. It cycles
explicit auxiliary source phases and Off, without choosing a body clip or
activating source actors. The native SetAssetEffectSequence API remains the
activation interface. GetAssetInfo reports phase names, active simulation,
particle counts, actual preview draw calls and independent errors. Drawing
counters reset when the phase changes. Effects remain off on a new model.

Current drawing is restricted to the standalone Model Viewer preview. Native
world body rendering remains available, but world particle queue submission and
actor/catalog phase activation have not been connected. The supplied four
death-model declarations/shared banks were installed separately as client-data
companions with source-model/hash checks and backups. They remain outside the
portable sourcekit runtime asset export; copying the user's model-data folders
to another client must preserve these adjacent companions.

## Verification scope

40static-pose inspection cases retain the prior particle motion/capacity values
exactly and match whole-interval/17ms partitions including appearance. Independent
io_m3 source-control calculations check452particle appearances and1808quad
vertices; maximum positional discrepancy1.31e-7. Native/standalone inspector
output matches. These checks establish source parameter routing/adapter geometry,
not original random/smoothing semantics or source pixel parity.

The native client was compiled and installed at the canonical requested path;
its exact-build pre-login integration probe is recorded separately in
verification/m3. Probe evidence must distinguish actual textured draw submission
and state restoration from unobserved visual appearance and in-world gameplay.
Accepted cloth solver settings were not changed.

Still incomplete: original-engine VFX fidelity, multi-frame/random atlas source
semantics, source material-bank ownership/animation, soft particles/depth blend,
source collision/forces/noise/parent velocity/mass modifiers, moving pose-at-birth
sampling, copies/ribbons/model particles and native world particle submission.
The full M3 goal is not complete.
