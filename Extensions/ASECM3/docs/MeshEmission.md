# Posed mesh emission surfaces

## Source contract

The eight particle definitions in the supplied four humanoid death meshes use
PAR_.emit_shape=7 and emit_type=4. The local source importer identifies these
as mesh-face emission and mesh-face-normal velocity. PAR_.emit_shape_regions
is an explicit U32_ list of region indices in division 0. The importer uses
the first division for these region references (io_m3_import.py, import
setup and particle-region binding). Its enum documentation states that mesh
vertex red can control the probability of emission (bl_enum.py,
particle_shape). These are importer-derived interpretations, not a recovered
complete original particle engine.

DecodeEffects now owns meshRegions for each particle definition, resolving
those references through the checked source Document. References are bounded
to 4096, charged to decoded-effect storage, and validated against the mesh's
first division. Invalid references produce the existing isolated effects error;
Scene still retains the body mesh/animation/physics. The ordinary skeletal
bank binder remains separate from the effect bank lane.

## Portable sampler

MeshEmissionSurface is a new core object. It snapshots only the named regions
from a supplied SceneFrame, whose positions may already include skeletal,
rigid or cloth deformation. It does not select a region by material, bone,
visibility, or similarity of names. In particular, a referenced region may be
hidden for drawing; its explicit emitter reference is still authoritative.

Face positions, normals and sample output remain in source model space.
Runtime world/model transforms must be applied once by the eventual particle
simulation/render adapter. Face normals are derived from the posed triangle
winding, not from smoothed vertex normals. Degenerate faces and zero-red
faces cannot emit. Missing color data uses SceneFrame's existing white default.
The sampler neither substitutes the emitter-bone origin nor silently emits
from another region when its source geometry is unavailable.

The area distribution uses area times maximum vertex red as its proposal.
Uniform barycentric sampling followed by rejection against interpolated red
produces density proportional to vertex red over the selected mesh surface.
The integral of that density is the triangle area times mean vertex red.
Sample returns the source region, triangle, barycentrics, position and unit
face normal. Random state belongs to the caller; xorshift32 gives deterministic
results. Each call is bounded to 64 attempts (per-proposal mean acceptance is
at least one third for a nonzero linearly interpolated red triangle).

The snapshot validates region identities, triangle completeness, indices and
finite positions/areas. It visits at most 250000 triangles across all explicit
references. Area() includes nondegenerate geometric area, WeightedArea() its
integrated red density, and Faces() counts retained nonzero-red triangles.
An empty/zero-density sampler returns false. Invalid source/frame structure
throws the module Error. No GPU or client state is referenced.

## Evidence and limits

ASECM3Inspect --emission-surfaces reports each mesh emitter's region list,
face count, areas and 32 deterministic samples. All 22 supplied files parse.
All eight emitters resolve to the source regions: region 0 for base/cowl/dark,
region 1 for Spooky18. An independent io_m3 parse and NumPy default-pose
skeleton/skin calculation match all face counts, areas and 256 sampled
positions/normals/barycentrics within recorded float tolerances. Standalone
and native-core inspector output is identical. Existing shared effect-bank
binding/rate inspection still succeeds for the base death mesh.

These actual source meshes have no vertex-color channel, so these checks do
not establish variable-red statistical accuracy. They also do not prove live
animated/cloth pose integration, actor clocks, particle emission/lifetime/
motion, billboard drawing or material ownership. Those runtime parts remain
incomplete. The sampler is not called automatically every frame; no new
per-frame scan or GPU work is installed. No native EXE was built/deployed and
the accepted cloth solver settings were not changed.

Evidence: effects-emission-surface-{corpus,reference,portable,portable-parity,
bank-inspection}.json and effects-emission-surface-{build,portable-build}.log.
