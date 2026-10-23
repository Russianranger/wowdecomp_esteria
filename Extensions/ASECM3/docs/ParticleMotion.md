# Particle lifetime and motion

Later implementation: [ParticlePreview.md](ParticlePreview.md) describes the
current appearance, native preview drawing and Effects control. The evidence
below records the earlier motion-only step.

ParticleEmitter is an owning portable CPU simulation object, built as its own
core object file. It retains an immutable Scene, one explicit effect owner/clip,
an EmissionClock, deterministic random state, and bounded live particle records.
It currently accepts mesh-normal billboard emitters (shape7/type4/particle0),
matching all eight supplied emitters. Unsupported forms report Error.

## Implemented behavior

Birth requests are processed at their authored sub-ms times. Old particles
expire before each birth applies emit_max. Over-cap requests are discarded at
that instant; they cannot become a later backlog. A surface is built lazily
only if the update has an accepted birth, from the supplied posed SceneFrame.
Samples use explicit emitter regions and source face normals.

Speed is sampled from emit_speed at the birth's source timestamp. Source flags
enable interpolation between emit_speed and emit_speed_random using one uniform
random value. Lifespan is sampled similarly from lifespan/lifespan_random.
These source values/enable flags are retained, but the uniform distribution
and alternate-endpoint interpretation are adapter policy, not established
original-engine random-distribution parity. Scalar animation uses the current
module's integer-ms evaluator (birth time is floored for these attributes).
Negative speed reverses the mesh normal. Lifespan must be in (0,3600] seconds.

The source world_space control selects stored world-space birth coordinates
and velocities, or local coordinates transformed by the current model-to-world
matrix when snapshots are requested. World-space particles retain their birth
transform as the emitter moves. The caller supplies the posed frame/transform;
births within an update currently use that update's supplied mesh pose. Moving
or cloth-deforming emission therefore still needs pose-at-birth/interpolation
work. Static-frame partition parity does not establish animated-frame parity.

Motion snapshots use an analytic constant-acceleration/linear-drag model:
dv/dt = (0,0,-gravity) - drag*v. Stable expm1/small-argument forms avoid
cancellation near zero drag; zero drag uses ballistic motion. Position and
velocity are evaluated from the immutable birth state, avoiding accumulating
per-render-frame integration error. Seconds, downward source gravity and drag
as an inverse-time coefficient are explicit adapter conventions awaiting live
source appearance/units comparison. They are not a recovered original solver.

Advance is transactional per emitter, including its clock/RNG/counters and
particle list. Invalid attributes/mesh/transform leave its previous state
available. Snapshots validate finite output. Live count is bounded to4096per
emitter; the native phase adds256emitter/16384live-particle/262144compiled-key
budgets. Empty-density samples increment a separate counter instead of moving
the emitter to an invented origin.

## Native preview API (compiled source candidate)

GxScene::SetEffectSequence(-1) stops the auxiliary phase; a nonnegative explicit
source index creates its bound emitters and resets only the effects clock.
The normal body animation/cloth/rigid clocks are independent. Effects advance
after body, rigid and cloth pose updates. The existing world-transform bridge
also records the transform for particle use. Current world actors do not select
an effect phase automatically.

AssetFrame registers SetAssetEffectSequence(index), returning normal success
or nil/error values. GetAssetInfo exposes effectClips, effectClip,
effectsSimulating, effectParticles, effectSpawned, effectDropped and
effectSimulationError. effectsRendering remains false. Status explicitly says
that particle drawing is pending. Runtime simulation errors disable the phase
and report once through the bounded built-in diagnostics with bank context.
No player-facing button or native particle draw call is enabled yet.

## Evidence and remaining scope

The existing inspector's --effect-particles option checks both emitters on all
four supplied death models at500/1000/2000/3333/13333ms:40cases. Whole-interval
and17ms-partition runs on the same static source frame produce exactly matching
counters/live states. Capacity stays16, zero surface failures occur, and every
one-shot phase drains. Native and standalone outputs match. An independent
io_m3 read verifies source capacities/lifespan ranges;1msRK4 integration between
192surviving-particle snapshots matches the implemented ODE, maximum component
error2.13e-7. This validates the adapter's numerical model, not original source
physics fidelity. Native core/client/AssetViewer and standalone libraries/tools
compile. The new native Lua activation method has not been exercised live.

Incomplete features include billboard/material drawing, particle color/size/
rotation/flipbooks, source collision flags/terrain/objects/child emission,
force fields/noise/parent velocity/distance limits/mass-gravity modifiers,
copy/ribbon/model particles, original random/units parity, animated birth-pose
sampling and actor/catalog phase activation. No native EXE was built/deployed,
no game session restarted, and accepted cloth solver settings were unchanged.

Evidence: effects-particle-motion-{corpus,reference,portable-parity}.json and
effects-particle-motion-{build,final-build,portable-build}.log under
verification/m3. The installed CD258138client and immutablev5sourcekit remain
the prior runtime snapshot; current source candidates are ahead of that build.
