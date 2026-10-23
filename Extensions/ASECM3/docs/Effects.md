# Source effect definitions (in progress)

Effects.cpp is a portable ASECM3Core object. Scene owns EffectsData beside
materials, animation and physics. It decodes MODL.particle_systems/PAR_,
particle_copies/PARC and ribbons/RIB_ into owned EffectDefinition records.
The existing MaterialRecord container is reused for typed scalar/reference
snapshots; emitter data is not interpreted as a material or shader.

Each definition retains its exact source bytes, schema/version, global bone,
material reference where applicable, nested animation-reference defaults,
IDs/interpolation modes and all scalar leaves. Its dependency graph is retained
once per source section, growing only if another reference requests more records.
Referenced model paths, region/copy arrays and spline data therefore remain
available without borrowed file/Document pointers. Resolving a reference does
not establish the simulation semantics of its contents.

Root counts are limited to4096each; decoded metadata and retained payloads each
have a64MiBbudget. Dependency traversal is bounded to65536sections and a1million
pending-reference queue, and skips per-record walks for primitive arrays without
references. Skeleton/material bounds are validated. Unknown effect-dependency
layouts, invalid fields/references and nonfinite numeric data report errors.
The documented positive-infinity flipbook reciprocal sentinel is retained as
bits with kind float_positive_infinity_sentinel; it must not be used as a normal
numeric sample. It is not a blanket exception for arbitrary NaN/Inf fields.

Scene isolates an effects decode error from the mesh renderer, preserving the
model/animation/physics data. GxScene reports that error once through the existing
bounded built-in diagnostic logger, with the model path, and includes it in
status. It does not hide geometry or invent effect material/bone indices.

## Current supplied-file evidence

The22supplied files contain8particle systems:2in each humanoid death/ragdoll
variant. They contain no particle copies or ribbons, so those branches have
schema support but no real-file corpus coverage here. All8definitions are
PAR_v24with emit_max16, emit_shape7, emit_type4and particle_type0. Each has
source animation references for its emission controls. These enum values are
retained; they are not yet interpreted by a particle simulator.

The extended ASECM3Inspect --effects mode exposes exact record/payload bytes and
typed properties, with summary counts available in ordinary mode. Independent
m3studio parsing compared all8records,3648scalar leaves and12000dependency bytes
across22files with no mismatches. A final rebuilt inspector exactly matched the
independently checked output. The standalone portable core build produced the
same full effects-inspector output as the native core build. Existing18mesh
cloth-regression output is unchanged, including the opt-in air fixtures.

Evidence:effects-current-corpus-audit.json,effects-source-emitter-audit.json,
effects-reader-{inspection,reference-parity,final-identity,cloth-regression,
portable-inspection}.json and effects-reader-{build,final-build,portable-build}.log
under verification/m3.

## Runtime work still required

This reader does not emit or draw particles/copies/ribbons. Animated controls,
spawn/burst timing, randomization, shape7/emit-type4interpretation, billboard
geometry, source smoothing/flipbooks, world/local-space lifetime, force masks,
collision/trails, effect material passes, bounds/sorting and source actor
activation still need implementation and runtime/source appearance checks.
Existing Full materials / effects pending status remains accurate.

The source/client-library candidate includes this reader and the preceding
source-layer-clamp correction. Neither is in the installed CD258138client or
its immutable v5source/runtime package yet. No native EXE was built/deployed
for this reader step. No game data, configuration or accepted cloth settings
were changed.
