# Particle births on observed solved geometry

`EmissionSurfaceInterval` is part of ASECM3Core. It owns two endpoint copies of
the triangles explicitly referenced by a Mesh emitter. It has no client, GPU,
file, Bullet or mutable Scene dependencies.

## Native update contract

GxScene retains the previous solved SceneFrame by move, only for the duration of
an update with an active effect phase. It evaluates the next animation frame,
then applies rigid and cloth solvers in the existing order. The particle callback
receives both solved frames and the effect interval's begin/end timestamps.

For each accepted particle birth:

1. ParticleTransformInterval samples the observed actor transform at that time.
2. EmissionSurfaceInterval interpolates the referenced triangle's model-space
   vertex positions at the same time.
3. Sampling uses the triangle's interpolated area and source vertex-red density.
   The face normal is recomputed from those positions.
4. ParticleEmitter uses this point, normal and actor transform as the birth state.

The interval surface is built lazily, once per emitter per update that actually
requests a birth. Frames are not copied with their UV arrays, and no additional
persistent frame is retained. Effects Off does not create mesh history.

The native viewer info includes `effectSurfaceBirthSamples`. The module world log
includes `birthSurfaceSamples` and `surfaceHistory=linear_observed` alongside its
existing actor-transform and draw counters.

## Sampling distribution

Each linearly interpolated triangle has edges `u(t)=u0+du*t` and `v(t)=v0+dv*t`.
Their cross product is a quadratic:

`C(t)=cross(u0,v0)+(cross(u0,dv)+cross(du,v0))*t+cross(du,dv)*t*t`.

For `0 <= t <= 1`, half the sum of the three coefficient lengths bounds its
area. A face proposal uses this bound times maximum vertex red. After uniform
barycentric sampling, acceptance multiplies instantaneous-area/bound by
interpolated-red/maximum-red. The resulting density is proportional to source
red over the interpolated mesh surface, without rebuilding a full CDF per birth.

The bound includes faces that are degenerate at both endpoints but have area in
between. Proposal construction validates finite values and overflow. Sampling
has at most 64 rejection attempts, matching the bounded nature of the existing
static surface lane. Very degenerate intervals can exhaust those attempts and
report no surface; they do not substitute an emitter origin or a stale face.

## Guards and limits

- Mesh shape 7 and explicit region references are required.
- Missing/duplicate region identities, changed index topology or vertex count,
  changed red density, partial triangles and invalid indices are rejected.
- At most 250,000 referenced triangles per interval, as in the static sampler.
- Non-finite/negative/reversed times and births outside the observed interval
  throw before random state or output are changed.
- Zero-duration intervals use the final observed geometry, matching the actor
  transform interval's endpoint convention.
- Construction/sampling does not alter either frame or physics state.
- The existing transactional ParticleEmitter callback and error logging remain
  responsible for failed updates. Successful output is published only after a
  complete accepted sample.

This is linear interpolation of two observed solved meshes. It is **not** exact
Bullet/cloth substep history, a physics rewind, an animation resimulation at every
birth, or proof of original HotS source pixel/physics parity. Large pose rotations
between updates follow straight vertex paths; exact substep history remains work.
Sampling is deterministic for identical observed endpoints, intervals and random
state. Changing the observation/update partition can change proposal bounds and
random consumption, so individual particle births are not claimed invariant to
frame partitioning on deforming meshes.
No accepted cloth or ragdoll parameters are changed.

## Current evidence

Portable core and native M3 libraries compile. No tests were added or run for this
change. The source/library-only v12 kit captured this work before runtime linking.
It was subsequently linked into the installed water scene-effects client
`8E1090CAA41B7EFFCFEF9A0ADE0587448EA4E0DD6972E547C82789DC8C3E5429`.
Startup readback does not prove this particle lane was exercised. Runtime
sampling, visual particle trails on moving cloth, FPS and source parity remain
unverified. Do not attribute prior particle acceptance to this sampler.
