# M3 physics integration

## Current boundary

Physics.cpp is a portable ASECM3Core object. Scene owns PhysicsData beside its
source skeleton and materials. The decoder follows the MODL physics roots and
retains their reference graph in owned byte buffers, once per referenced
section. It decodes body properties, collision shape transforms and primitive
sizes, legacy collision mesh arrays, joint frames/limits, cloth vertex tables,
constraints, influence maps and named coefficients. Unknown fields remain in
the retained source bytes. Runtime needs no Python or external format parser.

Snapshot and decoded-array budgets are each 64 MiB, with independent body,
joint, cloth, shape and vertex limits. Source references and finite floats are
validated by Document. Decode errors remain explicit; a physics decode failure
does not prevent the existing M3 mesh renderer from opening the model.

GetAssetInfo exposes physicsBodies, physicsJoints, physicsCloths, physicsError
and physicsSimulating. Validated PHCL mesh records now use the separate
ClothSimulation object described below. An explicit Ragdoll: On/Off control now
uses the separate RigidSimulation object for body/joint collision and skeletal
feedback. Automatic source state/event handling remains unfinished. See
RigidPhysics.md for its exact policy, source binding and limitations.

## Supplied-file findings

The 22 files beneath G:\Client335\Data\Patch-M.mpq\HOTS_M3 decode successfully.
alexstrasza_base contains 12 rigid bodies, two joints and one cloth record.
The cloth record describes 207 vertices (181 simulated), ten constraints and
an influence map of 332 vertices, from simulation region 1 to region 0.
Death-ragdoll variants contain up to 17 bodies and 15 joints. The source joint
types present are 1, 2 and 3. Both rigid-body and cloth paths are required.

The humanoid mesh cloth packed bone bytes cannot all be used as local indices
into its skin_bones array: real weighted entries exceed that array's length.
The local m3studio importer/exporter now confirms that packed cloth bone bytes,
PHCC bones and PHYJ endpoint bones use global skeleton indices. PHAC lookups
are four packed 16-bit simulation-vertex indices with four 8-bit weights. The
four humanoid meshes match this interpretation and exact region vertex counts.
Joint frame/axis and body activation semantics still require resolution.
The PHCL comments in structures.xml include hypotheses and are not proof of
runtime semantics. RequiredAnims documents also contain cloth records, so
mesh/bank identity and ownership must be established before merging them.

Evidence: verification/m3/physics-source-inventory.json. The standalone
ASECM3PhysicsInspect tool prints source-derived values and retained-byte counts.
Source schema: schema/structures.xml; user reference: SweetScape/010 Templates/M3.bt.

## Native cloth preview runtime

ClothSimulation.cpp is a separate core object; GxScene owns each instance. It
steps at 240 Hz with four constraint iterations, a 60-step cap and reset for
frame gaps over 0.25 seconds. Pins follow PHCL global skeleton weights. Dynamic
particles use triangle-area masses, gravity, exponential damping/tracking,
edge stretch, signed dihedral bending and mesh-path anchor tethers. Animated PHCC capsules
are transformed by their owning bone skin matrix. Weighted simulation
displacement maps through PHAC, preserving visible mesh-space offsets. Normal
and tangent frames are rebuilt after deformation. Clip changes rebase the
existing drape onto the new animated targets without injecting the pose jump
as velocity. Explicit reset, teleport, scale change and long clock gaps still
reset state;
disabling cloth restores an independently evaluated animated mesh.

Fixed ticks sample both pins and animated capsules at the same timestamp,
including the prior render frame's fractional accumulator. Capsule surface
velocity uses the previous physics tick, even when several render frames fit
between ticks. World-frame changes transport both capsule histories along
with the particles. Capsule depenetration is excluded from reconstructed
particle velocity by enforcing zero relative normal speed for the final active
contact, retaining its frictionless tangential speed. Intermediate collision
displacements are not summed and subtracted from the whole velocity: opposing
fabric/capsule projections can turn that sum into a spurious tangent impulse.
Capsule affine interpolation can
disable a singular intermediate volume, as with an authored zero scale.

Pose tracking remains an exponential rate using the source numeric coefficient.
Its direct positional shift is excluded from reconstructed particle velocity;
otherwise a first-order follow operation becomes an implicit spring with
strength proportional to solver frequency. This is the adapter's rate policy,
not a recovered Domino coefficient-unit specification. Density is retained for
relative particle masses. Multiplying every particle mass equally cannot make
this PBD cloth feel heavier without a corresponding force/contact model.

## Angular bending and geodesic tethers (2026-10-16)

ClothBending.cpp computes signed hinge angles and analytic gradients across
adjacent triangles. Rest angles retain source curvature and fold side. The
angular constraint does not confuse planar stretching with a bend. Degenerate
hinges are skipped; nonmanifold simulation edges stop cloth with a diagnostic
while rendering remains available. Finite-domain checks bound the math.

ClothTethers.cpp builds shortest structural-edge paths from existing PHCL pins.
Each connected free vertex gets its nearest anchor and that path's bind length
as an upper distance bound (scale1, stiffness1). A folded/holey mesh does not
use a straight-line shortcut. Unanchored components get no invented pin. Body
collision is solved after the fabric constraints, so a tight conflicting
contact can still temporarily exceed a tether limit; physical source contacts
and self-collision remain unfinished.

Projection stiffness uses 1-(1-k)^(120*dt/passes). The120Hz reference retains
the adapter's previous nominal material response while240x4 keeps960fabric
passes/second. It is not a recovered HotS solver frequency. Source damping,
tracking, gravity and stiffness coefficients are retained numerically.
No optional inertia attenuation or guessed density preset was applied.

## Explicit aerodynamic environment (implemented, not enabled in native instances)

PHCL v4 local_wind is decoded as an optional source-local velocity. Older
versions retain hasLocalWind=false and a zero vector. PhysicsInspect reports
both, alongside the existing drag_factor/lift_factor/wind_scale coefficients.
ClothAerodynamics.cpp is a separate portable core object; no original client
source hook is required.

ClothSimulation::SetAir(fluidDensity, worldWindVelocity) opts an instance into
triangle-based drag and lift. Density defaults to zero, so existing native
instances keep the deployed behavior. The caller must provide density in
units consistent with source positions, source area masses and seconds.
There is no presumed SI or HotS density default. World wind is transformed
without translation, then added to local_wind and multiplied by wind_scale.
That wind combination is an adapter policy, not recovered Domino semantics.

The aerodynamic force model uses triangle area/orientation and mean relative
velocity, following the drag/lift relationships described in NVIDIA's NvCloth
solver documentation. It distributes a triangle impulse equally to vertices;
inverse mass determines their velocity change, while zero-mass pins remain
kinematic. Moving pins supply their current tick velocity. Triangles are
processed sequentially using current velocities. With geometry frozen for a
step, quadratic drag uses v/(1+k*|v|*dt) to avoid reversing the flow; the lift
direction update is normalized to avoid explicit-Euler energy gain. This
integration choice is ours; it does not reproduce the NvCloth source solver.
Zero area/relative flow/coefficient/density and fully pinned triangles return
zero impulse. Invalid domains and nonfinite intermediates report errors.

Eleven aerodynamic checks cover normal/tangent/co-moving flow, zero density,
degeneracy, pins, winding reversal, lift energy, strong drag, relative mass and
invalid domains. The four supplied cloth meshes also retain exact 30/120Hz
static partition agreement with an explicitly supplied density1/wind(1,0,0)
fixture. Those numbers are diagnostic inputs, not proposed gameplay settings.
No current cape appearance, unit calibration or gameplay-FPS acceptance follows
from these checks. Native instances do not yet call SetAir; source-specific
aerodynamic calibration and host environment integration remain unfinished.

References:
https://nvidiagameworks.github.io/NvCloth/1.1/Solver/Index.html
https://nvidiagameworks.github.io/NvCloth/1.1/UserGuide/Index.html

Coefficient provenance: the source schema names the field used by this adapter
`stiffness_blending`, and the local m3studio UI calls it "Blending". The field
value is retained, but interpreting it as angular bending stiffness is an
adapter assumption, not verified Domino behavior. Tracking-as-rate and damping
units likewise remain adapter policies. Do not present these mappings as
recovered source physics or tune other source fields to conceal that uncertainty.

## Optional offline motion diagnostics (2026-10-16)

EnableCollisionDiagnostics enables capsule sweep sampling and maximum capsule,
bending and tether projection measurements. It defaults off. When disabled it
does not run extra sweep queries or extra hinge-angle evaluations. Diagnostics
do not change particle/contact response; their measured CPU time must not be
used as normal runtime performance evidence. Resetting diagnostic counters does
not reset the physical simulation. Vertex identifiers are local simulator indices;
the capsule bone identifier is the source global skeleton index.

The sweep check transforms each particle's previous/current positions into the
corresponding previous/current capsule frames and intersects that local segment.
Only endpoints outside both capsules by a float-roundoff allowance count. This
can expose an outside-to-outside crossing, but is not a complete rotational CCD
test, self-collision detector or proof of collision safety.

In the supplied six-second Walk A fixture, after one second of warmup the check
found zero such crossings in 2,166,132 eligible sweeps. The largest capsule
projection was 0.0196651 source units against Bone_Torso. At 120 render updates/s,
the sampled acceleration peak occurs at 1633ms, free-particle ordinal9. Nearby
bending projections increase on simulator vertex33 while capsule corrections
remain below0.005 source units. This suggests investigating fabric motion too;
it does not establish angular bending as the sole cause. No CCD or coefficient
change was made on the strength of this diagnostic.

Reference basis: PBD section4.1 (angular bending independent of stretching),
NvCloth's reference-frequency correction and tether defaults, and Small Steps.
https://matthias-research.github.io/pages/publications/posBasedDyn.pdf
https://nvidiagameworks.github.io/NvCloth/1.1/Solver/Index.html
https://nvidiagameworks.github.io/NvCloth/1.1/UserGuide/Index.html
https://matthias-research.github.io/pages/publications/smallsteps.pdf

## Authored skeletal transitions (2026-10-16)

SEQS.ms_blend is retained as Animation.blendMs. Ordinary clip changes use that
duration to blend from the currently displayed canonical animation locals to
the incoming clip as it advances. Translation/scale interpolate locally;
rotation follows the shortest quaternion arc. The hierarchy, inverse-bind
deformation and normals are then rebuilt, followed by M3A-to-mesh binding,
skinning, rigid feedback and cloth. Matrix elements and deformed cloth output
are not blended. An interrupted transition snapshots its current blended
locals, so it does not jump to the prior transition's unblended target.

The outgoing local pose is held during this short transition; a complete
two-clock actor layer and source-specific blending curves remain unfinished.
Initial load, selecting bind pose, a zero-duration clip and explicit rigid
state changes establish the new pose immediately. Cloth rebase preserves the
drape at that boundary. Authored event timing belongs to the incoming clip;
this work does not blend events or introduce guessed emission events.
All44Alexstrasza RequiredAnims clips specify100ms. World diagnostics expose
transitionMs/transitionWeight independently of elapsed source playback time.

ASECM3AnimationBlendInspect checks hierarchy/rotation/endpoint/interruption
math and actual external-bank endpoints with cloth stepping. Its sequential
spell stress includes large source root translations (Spell E to Spell F)
and still produces large cloth excursions. These are explicitly reported;
finite output is not proof of a physically valid cape or actor-root policy.
Angular bending and tethering are implemented as described above. Calibrated
source aerodynamics remains unfinished.

This preview policy is based on
[Position Based Dynamics](https://matthias-research.github.io/pages/publications/posBasedDyn.pdf),
not a reconstruction of Domino. Source density, gravity, damping, tracking,
stretching and the assumed bending coefficient feed the listed operations. Numerical
response is not claimed identical to HotS. Horizontal/shear anisotropy,
source-calibrated aerodynamic forces, frictional response, self-collision, source skin-offset/
exponent controls, rigid-body interaction and cloth LOD/cross-model sharing
remain unfinished. Invalid bindings report errors instead of indexing bones.

Model Viewer defaults validated cloth on, with a Cloth: On/Off control.
GetAssetInfo reports physicsMode=cloth_preview, steps, particle/mapped counts
and maximum displacement. physicsSimulating here means cloth only; full rigid
body dynamics is not advertised. Bank-only cloth collision records without a
mesh influence map do not instantiate particles.

ASECM3ClothInspect checks all 18 mesh files (four contain cloth), including
static 30/120-FPS partition equivalence, explicit PHAC displacement mapping,
animated evaluation, finite movement and resets. Particle counts are
207/195/135/207, mapping 332/350/218/438 visible vertices. Static partition and
displacement mapping errors were zero. Evidence: cloth-runtime-inspection.json
and physics-cloth-binding-research.json. These checks do not prove source-pixel
parity, visual acceptance or live FPS.

## Remaining solver work

1. Resolve body simulation flags, joint type axes/frame conventions and mesh vs
   bank cloth tables against source import/export code and actual source bytes.
2. Extend the independently owned rigid preview solver with source-driven
   kinematic/dynamic state changes and verified convex/mesh collision.
3. Complete the cloth features listed above and establish source response
   parity, keeping simulation-only geometry separate from material batches.
4. Apply solved transforms after animation and before skinning; recompute normal
   and tangent frames. Reset state on model/clip change and discontinuous clocks.
5. Add fixed-step limits, source scale handling, cancellation/teardown and
   diagnostics. Verify against the exact supplied humanoid, dragon and ragdoll
   models and the existing M3-to-M2 render isolation boundary.

The full M3 objective, including working physics, remains incomplete.


## Cloth helper visibility

PHCL.simulationRegion and its PHAC influence links identify physics-only helper
regions in primary division 0. Draw submission skips only validated simulation
regions; their vertices and indices remain available to cloth and skinning.
Target regions continue through the normal material passes. Cloth On/Off does
not change this source-defined visibility.

Validation checks region bounds, matching PHCL/PHAC simulation indices, distinct
render targets, PHAC vertex counts and weighted lookup bounds. Helper flags must
contain 0x5 and exclude 0x8; targets must contain 0x8 and exclude 0x4. A region
cannot serve both roles. On any ambiguity all geometry stays visible, with a
bounded diagnostic list logged at scene creation and shown in viewer status.
Names, materials, bone roots, and flags without a link never hide geometry.
For Alexstrasza Base the explicit link suppresses region 1, retaining region 0.
