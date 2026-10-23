# Explicit auxiliary material animation

`EffectMaterialBinding.cpp` is a separate portable core object. An explicitly
declared effect bank may animate matching mesh material records without being
merged into the skeletal animation bank or a global animation-ID namespace.

Binding requires the material name/type, record role/occurrence, record
tag/version, and complete typed animation-reference signature to agree.
Only actual incoming channels participate. Active interpolation must agree;
inactive defaults and interpolation remain mesh-owned. Extra bank materials
belonging to other skin families are skipped. Ambiguous or incompatible matching
owners reject the material binding and retain a separate diagnostic, preserving
the body and particle dependency lanes. Integer overlays currently reject rather
than pretending to sample unimplemented typed channels.

Scene stores numeric record locations. At evaluation time these resolve to the
current immutable Scene; overrides apply to the exact owner record and field.
An unrelated copy receives no overlay. Private particle-material copies use an
explicit adapted-owner route with checked material identity and record location.
Evaluators borrow records and tracks, so the owning Scene/material must outlive
evaluation. No shared material values are mutated.

The explicit effect clip has its own clock, clamped for source one-shot clips or
wrapped for looping clips. Native preview, world preparation, picking, and
private particle materials call this route only while an effect phase is active.
Automatic actor/catalog activation remains unimplemented.

## Current evidence and release boundary

The four Alexstrasza death meshes each bind three alpha-layer `color_add` fields
from the shared Dissipate bank. The root MAT versions differ and unrelated root
IDs differ; those records are not broadly merged. The linked LAYR versions and
complete reference signatures agree. The inactive uint16 flipbook reference has
different interpolation defaults and is correctly left alone.

Native and standalone inspections match for 72 samples. An independent io_m3
decoder checks source ownership, complete layer reference IDs/types, clip time,
collection priority, initial values and float curves: maximum absolute numeric
error 2.2744244154715432e-8. Existing preview/world particle corpus outputs remain
exactly unchanged in eight model/mode runs. Core/client/AssetViewer libraries
compile. The initial audit script used an incorrect schema field name; it was
corrected to anim_ms_start/end and the complete audit rerun successfully.

The initial source candidate was later linked and installed as executable
FE8662A4D12CC75BAD34653ACB4955BF7C3816471B4CF9804666E327C7C38CBF.
The exact-build pre-login probe passed three owner bindings, particle
submissions and return-to-M2 with 67 render-state checks and no crash log.
These checks do not prove native dissolve pixels, malformed-owner coverage,
actual world GPU behavior, gameplay FPS or original-engine effect fidelity.
Cloth settings are unchanged. Immutable sourcekit v7 preserves the earlier
34DD1727 world-particle release.

Evidence: verification/m3/effects-material-overlay-{corpus,reference,
portable-parity,particle-regression}.json and corresponding build logs.

## Current native material integration

The current native release wires owner-scoped auxiliary material evaluation into
preview, world packet preparation, picking, and private particle materials.
Player effect info includes materialRecords/materialError, and the Player panel
surfaces material/dependency errors along with simulation/draw errors. The
pre-login probe explicitly requires the three expected dissolve-layer bindings;
its success is native phase/binding/state evidence, not pixel fidelity.
