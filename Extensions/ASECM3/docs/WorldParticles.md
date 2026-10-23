# World particle pass

The 34DD client includes this world pass and first-draw/error telemetry. Its
exact-build pre-login probe passes API guards, Player panel initialization,
preview particle submissions and return-to-M2/state checks (106boundaries).
An in-world particle session remains unverified. The older immutable C821
sourcekit v6 is the prior preview release.

## Spaces, sizes and bounds

ParticleSpace.cpp is a separate portable object. World-space particles retain
their birth model scale as well as their birth position/velocity. Local-space
particles take the current model transform/scale at snapshot time. Snapshot
visual.size now denotes world size. Identity-space preview outputs are unchanged.
The camera helper transforms world positions directly through the scene view;
it does not apply the moving actor transform to an already world-space particle.

Billboards carry a scalar square size. Uniform rotation/scale/reflection and
translation are supported; non-affine, singular, skewed and nonuniform scale
transforms report Error instead of silently selecting a scale axis. Native
WoW unit model transforms normally use uniform scale, but real-host coverage of
this restriction remains pending.

ParticleSourceBounds maps particle centers into the actor's current source
space and encloses every possible camera-facing square using a half-diagonal
sphere. Thus particles left behind a moving actor expand its spatial bounds.
WorldInstance merges these into world-object bounds only; the standalone
viewer fit/camera bounds are unaffected by an expanding particle cloud.

## Native queue and draw path

PrepareWorldPackets appends explicit per-emitter packets with cached immutable
world-particle snapshots, source-space bounds and source material priority/
transparency. They use the existing private type6 native queue, including
above/below-liquid transparent placement and clipping. They never merge with
opaque body batches or interpret an emitter ordinal as a body material batch.

UnitWorldBinding passes the scene's actual world-to-camera matrix into
WorldInstance. A queued particle packet transforms its cached world snapshot
into camera space, builds camera-aligned quads, and submits through the private
particle GxSurface under the same world GxRenderGuard. Existing target, viewport,
depth and scene environment remain owned by the host; there is no UI camera,
auto-fit, grid or clear. Surface opacity/fog/pixel clipping use the current
world environment. Per-particle lighting/soft-particle behavior is incomplete.

Particle snapshots, bounds, packets and draw errors are isolated from body
geometry. A bad particle pass latches a separate diagnostic and logs once;
it does not replace the mesh or its skeletal state. Actual boundary-restoration
failures still use the original fatal world isolation latch. World particle
draws are counted separately; total shader/fog/environment/packed geometry
counters include both body and particle surfaces.

World telemetry records the first successful particle draw and first particle
error once per explicitly selected phase. Existing periodic records include
phase, live/spawned/capped counts and actual world-particle draw submissions.
It does not append a line for every rendered particle/frame.

## Explicit world activation

SetUnitEffectSequence validates unit GUID/pointer ownership and a ready M3
instance, then selects an auxiliary phase independently of skeletal actions.
The separate WorldParticleScript object exposes ASECM3_GetPlayerEffects and
ASECM3_SetPlayerEffectSequence. One registration call was added to the existing
ASECPlayer_Register host hook; parsing, simulation, queueing, drawing and Lua
implementation remain module-owned.

The source Player panel adds an M3 effects button beside Reset settings. It
cycles explicit phases and Off, displays separate effect errors, and guards
older clients without these APIs. This is a transient visual test control;
movement/model config persistence is unchanged. It requires a player override
whose model has an explicitly declared compatible effects bank. The public
native unit API can also address DBC-selected NPC bindings, but NPC actor/catalog
activation is not implemented automatically.

## Evidence and limits

Existing inspector --world-particles runs the actual eight supplied emitters
at five timestamps:40cases. They are born with a translated/rotated2xactor,
then inspected against a moved/rotated3xactor and an independent camera. It
checks frame-partition parity and that source bounds contain all camera quads.
Independent NumPy/source-field calculations cover452particles, confirming
birth-size retention, world positions/velocities, direct camera transformation
and source bounds. Native and portable core output match; identity-space
preview output is exactly unchanged. The native module libraries, AssetViewer
and the modified host registration object compile.

The first world diagnostic run had a missing JSON closing bracket; its output
was rejected. That output formatter was repaired and the full corpus rerun
with conversion errors terminating the inspection. Only the repaired corpus
is evidence. Current source evidence does not establish real native world
queue placement, GPU draws, moving-pose births, real local-space assets, source
actor parity, original VFX/physics fidelity, WMO/terrain collision or gameplay FPS.
The numerical step initially left C821 unchanged. The later 34DD native build
is now installed and pre-login checked. Existing data companions remain intact;
the v6 archive is preserved as the prior immutable preview release.

Evidence under verification/m3: effects-world-particle-{corpus,reference,
portable-parity}.json and effects-world-particle-{build,final-build,
inspector-build,portable-build,portable-final-build}.log.
