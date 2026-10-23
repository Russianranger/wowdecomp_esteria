# Rigid-body preview integration

RigidSimulation.cpp is an ASECM3Core object. Its private State owns a Bullet
world, broadphase, solver, shapes, bodies, joints and fixed-step accumulator.
GxScene owns the instance and destroys it on selection/clear. Constraints and
bodies detach before their backing shapes/world, including construction failure.
No original M2 engine physics path is modified.

Bullet 3.25 is vendored under third_party/bullet at upstream commit
2c204c49e56ed15ec5fcfa71d199ab6d6570b3f5. Unchanged collision, dynamics and math
sources build into ASECM3Bullet.lib. No runtime DLL/download, Python or installed
Bullet package is required. The upstream zlib license is retained in LICENSE.txt;
source-manifest.json records vendored file hashes.

## Source binding and preview policy

The local m3studio exporter/importer establishes global PHYJ bone endpoints and
local shape/joint matrices. Each body uses its source bone frame. Authored joint
pivots must agree within 0.02 source units at default bind pose; they are not
silently snapped to replacement frames.

Source box sizes are half extents (m3studio cube vertices are -1..+1), sphere
size0 is radius and capsule size0/size1 are radius/segment height along Z.
Compound shapes retain local frames and scale. Density times summed primitive
volume produces preview mass; overlapping volumes are not unioned. Source
friction, restitution, damping and gravity factor feed Bullet. Nonuniform
sphere/capsule scaling, shear/reflection, zero-scale bind frames, legacy PHRB v2
and unverified PHSH mesh topology produce errors instead of primitive substitutes.

Joint types map to spherical, Z-axis revolute, cone and welded constraints.
Cone bases rotate source Z to Bullet X. Source hinge/cone limits are used. Weld uses source angular-frequency/damping spring coefficients; joint friction remains unfinished. Source collidable, stackable and ignore-local
bits determine collision filters; connected pairs do not collide. The static
preview ground uses the source model lower Z. Shared world/terrain collision is
not implemented.

## Connected-body preview policy (2026-10-16)

User visual feedback exposed dismemberment when the ordinary Alexstrasza base
mesh was treated as a complete ragdoll. That mesh has 12 PHRB records but only
two PHYJ records, connecting the head ornaments. Collision bodies alone are not
evidence of an articulated ragdoll.

The explicit preview now builds joint-connected components. A body is dynamic
only when its component includes its nearest source-body skeleton ancestor, or
when it is a jointed skeleton root. Otherwise it follows animation as a kinematic
anchor. Source no_simulation (0x80) also prevents dynamics. No joints are invented
for disconnected limbs. This is a conservative preview policy, not a decoded
HotS activeState enum or actor state machine.

The reported base model therefore has two dynamic ornaments and ten animated
anchors. Its separate deathragdoll asset retains 15 dynamic bodies and one
animation anchor. Unbound descendants of dynamic bodies retain their activation
local pose rather than continuing unrelated animation under a fallen limb.
Kinematic anchors interpolate to each frame's animation target during substeps.

Weld angular frequency is interpreted as rad/s, with k=Ieff*omega^2 and
c=2*zeta*Ieff*omega. Ratios above one are preserved; zero frequency uses a fixed
preview weld. Bullet applies timestep stability limits. Exact Domino response,
source friction and source state transitions remain unverified.

The solver steps at 120 Hz with 30 iterations. Gaps over 0.25 seconds reseed
from animation. Clip changes reset state. Solved body transforms drive their
bone and unbound descendants; skin, normals and tangents are recomputed. Cloth
then sees solved bone/capsule transforms. Turning ragdoll off restores source
animation. Runtime errors stop rigid preview and reach GetAssetInfo/status.

## Evidence and remaining work

rigid-runtime-inspection.json covers all 18 supplied M3 mesh files; 12 contain
131 bodies and 71 joints. These produce finite motion, preview-ground contacts,
bounded joint separation, matching static 30/120-FPS partitions and pose reset.
The corpus exercises boxes/spheres/capsules and revolute/cone/weld joints.
Cylinders and spherical joints are implemented but absent from these samples.
Native Glue results are recorded separately. These checks are not appearance,
source-engine response parity or live FPS proof.

Standard global simulation-event activation and animation velocity handoff now
use Events.cpp; see AnimationEvents.md for the exact source/fixture proof.

Principal compound mass frames are now implemented; see RigidMassFrames.md.
Source-engine mass/inertia fidelity still needs verification.

Explicit PHSH shape-4 hulls now have a source/library implementation using their
point sets and integrated hull moments; see ConvexPhysics.md. It is not yet in
the installed client or runtime-accepted. PHSH shape-5 mesh topology remains
unsupported, with no primitive/hull substitution.

Remaining: source actor/scoped-event activation, animated kinematic/dynamic state
changes and velocity handoff, source material presets, compound COM/inertia
fidelity, spring/friction behavior, convex/mesh collision, animated scale/zero
scale, impulses/world forces, shared world collision, visuals/FPS/clean quit/
device recovery. Full M3 support remains incomplete.

## Regression scope after user screenshot

rigid-anchor-inspection.json covers all 18 mesh files. The reported base model
also runs five seconds using its real RequiredAnims bank, cloth and rigid world;
all ten animation-anchor transforms must exactly match source animation.
Native probe stage 9 checks 2 dynamic/10 anchored bodies with cloth still active.
These checks do not establish visual fidelity; the earlier finite/contact/pivot
checks missed the disconnected-body policy error. User feedback on cloth is
partial acceptance; user feedback on the previous ragdoll is a visual failure.
