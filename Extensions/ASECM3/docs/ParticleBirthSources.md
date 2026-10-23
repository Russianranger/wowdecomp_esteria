# Sampling a particle source at its birth time

`ParticleEmitter::AdvanceSampled` accepts a repeatable callback receiving the
exact elapsed effect birth time and the emitter's transactional random state.
It returns an emission point and model-to-world transform for that time, or
false for an unavailable/zero-density source. The callback must not advance
external physics or otherwise mutate external simulation state. It is invoked
only for accepted births after expiration and capacity checks. Births discarded
by the live cap never trigger expensive surface lookup.

The emitter validates finite positions, unit normals and invertible affine
transforms, then captures the existing source-time speed/lifetime/appearance.
World particles retain each birth transform and visual scale; local particles
continue to use the current transform at snapshot time. Callback failures commit
none of the emitter clock, counters, particles or random state. External callback
side effects cannot be rolled back, which is why callbacks must be repeatable.

The existing `Advance(frame, transform)` API remains the explicit constant-input
route and lazily builds one surface per update. Its existing corpus outputs are
unchanged. Neither route fabricates physics history or interpolates an actor
transform implicitly.

## Evidence and remaining integration

ASECM3ParticleBirthInspect uses each supplied death mesh's source clip at integer
birth milliseconds and an explicit continuously translating/rotating/scaling
actor fixture. Four meshes, 40 cases and 1068 accepted birth samples match whole
interval versus 17ms update partition exactly; 40 callback-failure rollback
checks pass. Native/standalone core outputs match. 452 surviving particle
comparisons differ from using the single end-of-update transform.

The inspected death clip's emission geometry is static: the measured source
pose difference is zero. Thus this corpus demonstrates time-dependent actor
transform sampling, not an animated/cloth-deforming surface history. The fixture
does not establish original-engine VFX or physics parity. Existing 12
model/mode corpus outputs match in both native and standalone core builds.

This API began as an uninstalled source candidate. The later native actor-motion
step uses it with observed actor transform intervals, while the current solved
mesh remains constant during an update. See ParticleActorMotion.md. It still
needs actual solved-pose history for deforming surface fidelity; do not rewind
the live cloth/rigid solver from the callback. Executable FE8662A4 and immutable
sourcekit v8 preserve the earlier material-animation baseline. Consult STATUS.md
for the current actor-motion release and Stop effects UI deployment state.

Evidence: verification/m3/effects-birth-source-{corpus,portable-parity,
regression}.json and effects-birth-source-{build,portable-build}.log.
