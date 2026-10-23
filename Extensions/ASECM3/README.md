# ASEC native M3 module — implementation in progress

The objective remains full native M3/M3A reading/rendering, WoW animation
integration, and a transferable module with few core hooks. Current native
rendering includes animated standard/MADD surfaces and native world integration;
full source-format and engine fidelity is **not complete**.

Particle birth mesh sampling now has an observed solved-surface interval lane.
See [EmissionSurfaceHistory.md](docs/EmissionSurfaceHistory.md) for its contract,
limits and current compile/install verification scope.

## Owned components

- `ASECM3Core.lib`: portable C++17 document/schema, mesh/skeleton, materials,
  transform animations, pose/skinning, DDS, and owning scene objects. No Python,
  converter runtime, engine pointers, or generated on-disk M2 carrier.
- `Scene`: owns the source Document, ModelData, MaterialCatalog, AnimationBank,
  exact texture references and exact-name sequence/group associations. Frames
  retain bone poses, region-local indices, skinned positions/normals and
  decompressed UV channels. Batch material indirection is preserved.
- `ASECM3Client.lib`: `GxDDS` owns raw DDS resources and their callback lifetime;
  `GxScene` owns the native surface draw and same-document source-clip playback.
- `Modules/ASECAssetViewer`: generic native frame host for M3/WMO/ADT/BLP. Its M3
  adapter uses the owning Scene and GxScene. The original M2 pointer stays null;
  original M2/dressing-room draw paths are preserved.
- `schema/structures.xml`, `schema/M3.bt`: credited schema and supplied template
  snapshot. Generated metadata hash is checked by CMake.
- `data`: exact manual policies, actor context and dependency research. These
  records are not a complete inherited runtime actor/event graph.

Host hooks cover the FrameXML factory, Model2 queue/model queries, and unit
lifecycle/animation/world teardown. The implementations and owned state stay
in this module. Each Lua context owns its metatable reference. The data core
builds after copying only `Modules/ASECM3` to another build tree. See
[PORTING.md](docs/PORTING.md) for the host contract and transfer kit.

## Current runtime boundary

Standard and decoded MADD named-input surfaces support diffuse/team color,
normals, specular/gloss, environment maps and source composite ordering.
Native world draws consume per-object light/fog/opacity and liquid clipping
through isolated D3D9/9Ex submissions. Unsupported classes/channels remain
diagnostic. Full source material/VFX/HDR/shadow equivalence is unfinished.

Source clip selection uses the zero-based M3 sequence index, with `-1` for the
default pose. Only unique exact-name transformation-group associations in the
same source Document are accepted. Non-looping clips hold their final source
time; looping clips wrap within the declared source interval. These indices
are **not WoW animation IDs**. Independent global/concurrent clocks and external
actor-layer/event semantics remain unfinished. External M3A binding is supported
with explicit mesh/bank ownership, topology/name/bind validation and source
sequence-group checks; world action mapping uses the copied research policies.

Texture loading tries the original virtual path first, then the explicit private
`<client>/ASECM3/Assets/Textures` source. Source references are normalized before
SFile reads. One DDS dependency is loaded per client update. No source texture
is converted to BLP. Loose sidecar DDS files are read directly from disk with a
128 MiB bound; missing sidecars continue through the virtual asset lookup.
Existing resources are backed up during installation.

## Build

From a VS C++ developer environment in the client tree:

```
cmake --build build/x64-ninja --target WowClient ASECM3SceneInspect --parallel 12
```

For the portable core and inspectors, without the client or Python:

```
cmake -S Modules/ASECM3 -B build/m3-portable -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/m3-portable --parallel 12
```

`WOWDECOMP_ENABLE_ASEC_M3` guards the M3 module. The engine adapter is included
only when the host provides Gx. Windows is the current engine integration host.

## Verification evidence

The authoritative reports are under `WoW335_ASEC/verification/m3`:

- Reader: 22 actual files, complete mesh/skeleton/sequence comparisons and six
  malformed variants rejected.
- Pose: 502 source-time cases, 281,311 skinned vertices, full world/skin/normal
  matrices; maximum error approximately 1.568e-6 against independent source math.
- DDS: 47 real BC1/BC3 files, 647 face/mip ranges and six malformed variants.
- Materials: 49 materials, 774 layer records, 33 batches, 12 composite sections,
  105,019 scalar properties and four opaque buffer payloads; five malformed
  material/batch/graph variants rejected. Buffer instruction semantics pending.
- Scene: 822 cases, 410,991 vertices, 649,777 UV pairs and 40,327 bone world
  matrices; source references/indices/batches checked against M3Studio, pose
  integration checked through the independently verified pose inspector.

Reports apply only when their recorded tool and source hashes match. Numerical
checks do not prove GPU execution, visual fidelity, input usability or FPS.
Runtime probe results are recorded separately in STATUS.md. Capture failures
must not be treated as visual success.

## Package and deployment

`deploy/ASECM3Assets/ASECM3-ASSETS.json` records 22 original models/banks and 47
raw DDS dependencies (69 files, approximately 54.42 MiB). Runtime deployment uses
`deploy/ASEC335/ASEC-M3-SHA256.txt`: the canonical EXE, shared addon and private
DDS dependencies. `tools/Install-ASECM3.ps1` validates every staged hash and
allowlisted destination, backs up changed files, installs, reads back hashes,
and restores changed files on failure. `-Restart` targets only the exact
canonical EXE in the selected client root; `-Probe` additionally enables the
explicit native integration probe. Normal launches do not run that probe.

GPU objects must be created/used/destroyed on their owning render thread and
released before the graphics device. Full shutdown/device recreation and
Glue/World resource lifecycle remain required acceptance gates.

## Remaining full-support gates

- Full standard/composite/node material shaders and compositing, tangent/sign
  and vertex colors, alpha/depth effects, environment sampling and color space.
- Native WoW action mapping with current manual precedence and expected-name
  guards; dependency inheritance and compatible body/skin/facial bank binding.
- Independent global/concurrent clocks, non-transform/material channels,
  selective inheritance, billboards, events, particles, ribbons and lights.
- Verified appearance, controls, performance, teardown and device recovery in
  both Glue and World; complete transferable installation/rollback acceptance.

See STATUS.md and RENDERER-INTEGRATION.md for progress history and open gates.

## Automatic RequiredAnims binding

M3 models without embedded source clips now look for M3A banks automatically
when selected. Lookup tries `<model>_requiredanims.m3a` in the model folder,
then the corresponding sibling bank folder and flat parent folder. It removes
trailing underscore-delimited model/skin suffixes progressively and repeats the
search, so `alexstrasza_base` can find `alexstrasza_requiredanims`. Snapshot
reads remain on the client thread, one candidate per update; parsing, identity
checks and binding preparation run on the asset decoder worker.

`BankBinding` validates unique bone names, typed animation channel ownership,
shared-ancestor topology and skin-weight/source-bank coverage. Identity-rest
static container nodes can be omitted when all their source keys preserve their
defaults. An animated hierarchy conflict is rejected. The source deformation
`animatedBankWorld * bankInverseBind` is applied to the mesh's own bind armature;
extra mesh bones inherit their nearest verified ancestor's deformation. Mesh
and bank bone indices are never assumed equal, and different bind proportions
are not overwritten with another model's skeleton.

The scene owns the bank and its source sequences. A matching Stand clip starts
automatically, with the first playable source clip as fallback. The UI updates
the animation count and clip selector and reports the selected bank. `-1`
restores the mesh default pose. Existing embedded clips retain their source.

Current binding evidence: Base and Dark -> body bank (44 clips), Spooky18 ->
its skin bank (45 clips), Dragon Base -> dragon bank (30 clips). All four cover
100 percent of skin weights through verified mapped ancestors. Twenty-seven
sampled cases and 198,936 vertices match independent source pose/skin math;
cross-paired body/dragon banks are rejected. Native Glue instrumentation confirms
automatic body-bank discovery, Stand playback, DDS upload/draw submission and
retained camera state. This does not establish pixel fidelity, facial-bank
layering, RequiredAnimsEx inheritance or general WoW semantic action mapping.

## Native Wrath action resolver

`Semantics.h` / `Semantics.cpp` now map source clip names to Wrath actions using
the source-backed HotS token/context research. The portable core owns generated
metadata from `data/animation_data.csv`, `hots_animation_contexts.json`, and
`animation_id_overrides.json`; no converter or Python runtime is needed by the
client. `tools/generate_semantics.py` refreshes the checked-in native table after
editing those copied inputs. CMake rejects stale input hashes.

Precedence is guarded model/source index, model name policy, default name policy,
then automatic semantic classification. An indexed policy requires its expected
source name to match; disagreement disables action selection with an explicit
error while original source-clip preview remains available. Hero-specific spell
letters are not treated as universal action numbers. Only exact body actor
queries establish stealth/weapon-attack context. This catalog does not yet
resolve the complete inherited actor graph.

The viewer's **Clip / WoW ID** control switches between original source clip
indices (`-1` = default pose) and Wrath IDs (`0..505`). `SetAssetWoWAction(id)`
selects an eligible mapped source clip; unavailable IDs return an error and keep
playback unchanged. Selection uses highest policy weight then source order for
deterministic preview, not random gameplay variation. Excluded clips, zero-weight
clips and global clips are not normal action candidates; raw source selection
retains access to them. Global/concurrent playback clocks are still pending.

`semantics-parity.json` records 1,643 source-corpus/body-query cases against the
converter, with two stale source-index rejections. The native Glue probe verifies
Walk -> source clip 20 for Alexstrasza Base, Stand restoration and unchanged
playback on an unavailable action. These are classification and instrumented
playback checks, not full material fidelity or in-world visual acceptance.

## Composite passes and material animation

`MaterialEvaluation` is portable core code. It evaluates typed scalar, color and
UV/vector references from the mesh's own source animation groups, honors STC
priority/tie order, and expands composite proxies into their actual MATM child
passes. Opacity-zero children are skipped. Reduced opacity converts an opaque
child to alpha blending. Opaque-first composites preserve their layer order;
transparent-first composites defer alpha passes into model-wide priority order.
Terrain/creep placement follows the documented composite ordering, while their
unsupported leaf shaders still use the existing diagnostic rendering.

Sources: Blizzard Art Tools [SC2 Composite](https://mapster.talv.space/star-tools/Material_SC2Composite.html),
the copied structures.xml metadata and supplied M3.bt, and actual source records.
Native drawing now renders the supported standard children over the same mesh,
with evaluated opacity, constant color, multiply/add/brightness and UV transforms.
The complete SC2 standard-layer shader (normal/specular, masks, emissive,
environment and dissolve), MADD node shaders and effects remain unfinished.
Unsupported MADD geometry now uses neutral vertex-lit shading when no supported
surface or exact diffuse fallback is available, instead of the magenta
diagnostic color. This preserves geometry visibility; it does not imply that
the MADD shader has been decoded.

Skeletal RequiredAnims compatibility does not prove that its material IDs belong
to the mesh. An external bone bank therefore never animates mesh materials by
numeric ID coincidence; these references retain their mesh defaults until a
separately verified material-bank association exists. Same-document material
animation uses the selected source timeline. Global/concurrent clocks remain
separate unfinished work.

`material-evaluation-parity.json` records 138 cases, 56,708 sampled values, 104
values with active source tracks, and 251 pass checks across the supplied M3
corpus. A scratch-file COL fixture verifies BGRA decoding and linear RGBA
interpolation at three times; original game files are not modified. These checks
establish source evaluation and ordering, not rendered pixel fidelity.

## Embedded standard-layer GPU shader

`client/GxSurface` owns an initial standard-material layer shader. Its VS/PS
bytecode is compiled into the module object files from
`shaders/StandardSurface.hlsl`; the client needs no runtime compiler or extra
shader files. `tools/generate_surface_shader.py` uses the Windows SDK's fxc when
maintaining the shader. The host CMake build rejects stale HLSL/bytecode hashes.

Implemented layers: diffuse, decal, emissive 1/2, alpha mask 1/2, lightmap and
ambient occlusion, with per-layer channel selection, sampled source values and
independent UV0..UV3 transforms. Diffuse RGBA selects the team-tint mask; the
preview currently uses an explicit fixed blue team tint. Emissive supports the
documented Mod/Mod2x/Add/Blend/Team Color Add modes. Zero-opacity and alpha-cutout
fragments are discarded before color/depth writes. This is still preview
lighting: normal/specular/environment/parallax/fresnel, source vertex colors,
MADD graphs, other material types, effects and full game lighting are pending.
Generated UVs, unknown channel/mode operations, cube input in a 2D slot and
conflicting wrap states on one shared texture retain the previous fallback and
report a limitation rather than inventing shader bindings.

Shader resources use the existing device shader cache under module-prefixed
names. Each draw restores render states and its VS/PS constant ranges; native
probe counters compare the resulting Gx shadows with their saved values. This
does not substitute for device-loss, clean game quit or in-world visual checks.

`surface-gpu-fixtures.json`: 18 D3D9 HAL readback fixtures, maximum one byte of
error, covering constant colors, channels, decal/emissive operations, two masks,
cutout/zero alpha, team tint and four source UV sets. Native pre-login probes
also check actual client shader submission and Gx shadow restoration. Windows
capture failed twice, so actual source-model pixel appearance is unverified.

## Tangent basis, normal maps and vertex colors

The core now retains source packed/float tangents, the packed normal handedness
byte, and optional vertex RGBA (decoded from BGRA). Skinning uses forward linear
deformation for tangent/bitangent directions, inverse transpose for normals,
then orthogonalizes the tangent and derives the reflected handedness. Zero-scale
or unknown-sign bases remain invalid instead of producing an invented frame.
Scene owns and forwards these values through region-specific palettes.

The native shader now supports BC3N/DXT5N normal textures on UV0 using the source
tangent frame, including UV rotation/mirroring. SC2 normal X comes from alpha
and Y from green. Missing/degenerate bases retain geometric-normal lighting.
Other normal UV sets, non-BC3 inputs and nondefault normal color operations remain
explicitly unsupported. Material vertex-color and vertex-alpha bits independently
control source color application; neither is assumed enabled merely because the
vertex layout contains color. Lighting is still the preview's directional model.

Evidence: 28 GPU fixtures (max byte error 1), six analytical basis fixtures for
nonuniform/reflected/zero scale and source sign, reader checks on 22 real files
plus six malformed files, and a scratch 7,186-vertex color-layout fixture.
Same-document pose/scene checks now compare tangent/sign/color values as well as
the existing data. This does not establish source-model visual acceptance,
general world lighting, specular/environment/parallax/fresnel, MADD or effects.

## Specular and gloss preview response

The embedded shader now binds `layer_spec` and `layer_gloss`, including shared
texture RGB/alpha usage, constant specular colors, sampled layer operations and
independent source UV transforms. It retains the material's `specularity`,
`hdr_spec` and RGB/scalar-alpha selection. Gloss adjusts the exponent, and unlit
passes bypass specular inputs. Degenerate camera/view vectors use a defined
fallback. Render state capture includes all eleven texture slots and the
complete 59-vector pixel constant range touched by this shader.

These controls drive the preview's explicit Blinn-Phong response:
`specular * HDR * max(N.L,0) * max(N.H,0)^max(exponent*gloss,0.0001)`.
The normal can come from the source BC3N normal map; the view ray comes from the
camera-relative vertex. Gloss defaults to one when its layer is absent. This
maps the documented qualitative controls to preview lighting; it does not claim
SC2 lighting, BRDF, HDR or tone-mapping pixel equivalence.

Reference: Blizzard Art Tools [SC2 Standard](https://mapster.talv.space/star-tools/Material_SC2Standard.html),
plus the supplied material records (including exponent 512, HDR 2/3, shared
Spec DDS RGB/alpha and constant Spooky18 specular colors). Forty GPU fixtures
cover prior behavior and exponent, gloss, HDR, scalar input, view direction,
unlit behavior and camera-at-vertex fallback. Source-model visual acceptance,
world lighting, environment/parallax/fresnel, MADD, effects and animation layering
remain unfinished. Earlier sections record their feature state at that build.

## Environment maps and masks

The standard surface adapter now binds cube environments through a dedicated
cube-sampler shader and 2D environments through the 2D variant. Both are embedded
bytecode. Source `uv_source=2` selects reflection-cube lookup; source 3 selects
an explicit D3D camera-space sphere projection. Ordinary UV0..UV3 2D maps are
also supported. Sphere projection and cube orientation still need comparison
against source-engine model pixels; fixture agreement alone does not prove it.

Environment layers retain channel/color operations, RGB/scalar masks, the four
Mod/Mod2x/Add/Blend modes, the constant HDR multiplier and basic normal/inverted
Fresnel controls. Generated cube UV transforms, advanced Fresnel transforms and
nonzero diffuse/specular HDR coefficients report a limitation. This extends
preview lighting and does not establish SC2 BRDF or HDR/tone-mapping parity.

Scoped restoration now covers thirteen textures and 74 pixel constant vectors.
The DDS adapter tracks valid uploads of all six cube faces. The native probe
requires a resident six-face Base environment, Dark sphere-map submissions,
zero invalid DDS requests, and state/constant restoration before passing.
`surface-environment-gpu-fixtures.json` records 59 HAL readback fixtures (maximum
byte error 1), including all six cube faces, four sphere quadrants, environment
blends/masks/basic Fresnel and all previous surface cases. Native probe evidence
is recorded separately in `runtime-environment-probe.json/log` when available.

The full objective remains unfinished: complete material fidelity and MADD,
parallax and advanced Fresnel/normal operations, effects, animation layering and
actor dependencies, gameplay dispatch, source-model visual and World acceptance,
performance, quit/device recovery, and final transfer acceptance remain open.

## MADD indexed parameter storage

`BufferMaterial.cpp` now decodes the indexed parameter groups found in the
supplied cowl and RequiredAnims MADD materials. It retains opaque IDs, byte widths, flags, original
offsets and owned value bytes; validates bounds/overlaps/count budgets; and
cross-checks the separate root identifier list. Unrecognized layouts retain
their byte-exact payload plus a decode error. The native viewer exposes decoded
group/field/error counts and states that shader bindings are pending.

This is a data decoder. It does not yet evaluate the MADD shader or identify
roles from texture filenames. See [storage layout and evidence](docs/MADD-parameter-storage.md).
Both host and standalone builds pass 24 structure/malformed fixtures and match
25 groups/58 fields from the two MADD materials across the 22-file corpus.
Existing material/reference byte checks also pass. MADD rendering and the other
full-objective gaps listed above remain unfinished.

## MADD named-input preview surfaces

The later BufferSurface.cpp adapter now routes the cowl and RequiredAnims
MADD materials through the native surface shader. Block and parameter IDs are
matched to named metadata from four agreeing sections of the installed HotS
DX11 shader cache. The small metadata table is checked and embedded in the core
object; the cache is not a runtime or deployment dependency. Original source
records and parameter bytes remain intact beside the immutable preview surface.

The seven source layers bind diffuse/team color, BC3N normal, gloss, specular,
emissive, environment mask and cube environment. Source specular exponent and
TwoSided presence are retained. Unsupported sampler/control/UV variants and
dynamic parameters report a limitation. This still uses preview lighting:
ECSpec, source HDR/finalization, Fog/FOW and general MADD graphs are unfinished.
Identity UV transforms and the supplied invert/clamp settings are currently
supported; their general operations need more decoding.

Host and standalone binding checks pass two source materials/14 layers, 12
unsupported input cases and six channel/gain routes. Native Glue cowl probing
records four layer-shader draws including normal, specular and cube inputs,
with zero state failures. Shared GPU fixtures pass 60 cases, including MADD
channel clamping before AO composition; fresh source scene
regression passes 822 cases. The attempted Windows captures both timed out,
so source-model appearance is unverified. The full M3 objective remains active.

## M3 render isolation regression fix

The asset-viewer integration now wraps the complete M3 callback, including its
camera and grid, in PreviewRenderGuard. M3 uses private vertex/index streaming
pools so its larger stride and buffer growth cannot resize or discard the shared
M2/world pools. The guard restores actual D3D9 state and shader constant banks,
Gx software state, primitive bindings, transforms and backend caches separately.
Restoration failures are logged and stop further M3 drawing for that frame.

The native Glue regression passed 24 boundaries with zero failures before
switching back to ana_army18.m2. It compares actual API state, software caches
and shared buffer metadata; the initial state-block-only attempt caught a real
vertex constant mismatch and was rejected. Evidence is recorded in
verification/m3/runtime-render-isolation-probe.json/log. These checks do not
establish pixel appearance or in-world visual acceptance.

## Physics data foundation

Physics.cpp now owns decoding of PHRB bodies, PHSH shapes, PHYJ joints and
PHCL cloth tables, retaining the referenced source graph within memory budgets.
Scene exposes PhysicsData; the native frame reports counts/errors separately
from physicsSimulating. All 22 supplied files decode
(131 bodies, 71 joints, six cloth records). Native Glue loading and the existing
M3-to-M2 render boundary regression pass.
See docs/Physics.md for source findings and the remaining rigid-body/cloth work.

ClothSimulation.cpp now provides native fixed-step preview cloth dynamics with
animated pins, capsule collision, PHAC displacement mapping and rebuilt normal/
tangent frames. Model Viewer includes Cloth: On/Off. All four supplied humanoid
cloth bindings pass the source-file stepping/mapping/reset checks. This is a
preview PBD policy, not Domino parity. Rigid-body dynamics and advanced cloth
features, source visual acceptance and live FPS checks remain unfinished.

RigidSimulation.cpp now provides explicit Ragdoll: On/Off preview using the
statically linked, module-owned Bullet 3.25 solver. Source primitive shapes,
joint pivots and hinge/cone/weld constraints drive native skeletal skinning,
followed by cloth. All 18 supplied mesh files pass the runtime inspector;
native Glue ragdoll and M3-to-M2 isolation checks pass. Source physics activation
events, advanced dynamics and actual visual acceptance remain unfinished.
See docs/RigidPhysics.md and third_party/bullet/LICENSE.txt.

Events.cpp now provides source event timeline dispatch, with owned SDEV records
and lossless SDU3 integer tracks. A standard global Evt_Simulate can activate
the private rigid world with animation velocity handoff. The stock 22 files
contain only SeqEnd events; their HotS actor/state activation remains unfinished.
See docs/AnimationEvents.md for exact behavior and diagnostic-fixture scope.

The 2026-10-16 ragdoll correction keeps disconnected collision bodies animated.
Only source-jointed skeleton chains become dynamic; ordinary Alexstrasza base
has two simulated ornaments and ten animation anchors. Use the authored
DeathRagdoll asset for its articulated full-body rig. See docs/RigidPhysics.md.


Model Viewer exposes M3 region visibility through Geometry. Individual rows,
All, None and Defaults use native GetAssetRegions/SetAssetRegionVisible methods.
Rows show zero-based region indices, vertex counts and helper status; selection
shows triangle counts and source flags. Visibility belongs to the loaded GxScene,
survives animation/physics updates, and resets with a new scene. It gates every
material pass for that region without deleting or changing simulation geometry.
Validated PHCL/PHAC helpers are listed as hidden and cannot be enabled by All.
M2 geoset and texture controls retain their existing native paths.


WorldInstance.cpp and GxGeometryTransform.cpp now provide a module-owned camera-
space draw API with current skinned bounds and explicit semantic actions. Shared
render isolation moved into client/GxRenderGuard.cpp; the viewer uses aliases.
This is not connected to world object/DBC dispatch yet. See docs/WorldIntegration.md.

### World action playback compatibility

Runtime semantic identity is the mesh filename stem, including when loaded from an absolute disk path. Bank filenames never choose mesh policies. World Run/Sprint requests prefer exact mapped clips, then use eligible body locomotion when a source bank lacks those actions; this fallback is logged and does not relabel source clips. Alexstrasza's RequiredAnims bank provides Walk A (clip20), but no separate Run take. Persistent WoW Dance (69) repeats its mapped source take; explicit raw viewer playback still follows source looping flags. Unsupported world actions retain the current pose and report the missing mapping once per action transition.

### Native world bounds bridge

The CM2Model bounds getters now consult UnitWorldBinding only for registered M3 proxies on the host thread. Current bounds come from visible animated/skinned and simulated vertices, excluding explicit cloth simulation helper regions. Maximum bounds accumulate the observed pose envelope per instance. Local queries apply the established HotS-to-Wrath basis; current world queries additionally use the proxy's actual world/attached transform. The module validates finite transforms/extents, transforms all eight corners, and supplies an outward-rounded enclosing sphere. Native M2 shared data is never edited.

The unit's spatial record is refreshed when its observed maximum envelope expands; local maximum extents are rounded outward to 0.05 units to avoid publishing tiny per-tick changes. This is an observed envelope, not an offline precomputed bound for every future animation. Worker-thread M2 queries retain their native path and do not touch the mutable M3 registry. Selection triangles are handled by the bridge described below. Native attachments, source shadows and source-specific collision bodies against terrain remain open.

### Native world lighting and fog

World draws now snapshot the object's CM2Lighting values into a module-owned SceneEnvironment: camera-space sun direction, ambient/diffuse/specular colors, up to four selected point lights with native attenuation, fog start/end/density/color and native fog distance scale, plus effective object opacity. No light/scene pointer is retained in the snapshot; sunlight finalization uses a value copy. Nonfinite native values stop and report the affected M3 binding once.

Standard and decoded MADD surface passes consume that snapshot. Preview calls supply no world environment and retain their explicit preview light. Source unshaded (0x10) and unfogged (0x4) flags remain independent. Fog is applied after vertex color and emission, with blend-neutral black/white/half-gray for additive/modulate/modulate2x passes. Radial camera distance uses the host start/end/density/scale; the material BRDF remains the module's existing Blinn-Phong approximation. Native SC2/HDR shader equivalence, world shadows and interleaved water/transparency pass ordering remain open. Unsupported-material diagnostic fallback is not evidence of complete material fidelity.

Surface shader interface v6 owns pixel constant registers c0..c91 and restores all of them after its draw. All three embedded shaders are regenerated from HLSL with the matching source hash. GPU inspection now includes 21 world-environment cases in addition to existing preview fixtures (81 total). This proves fixture pixels, not in-game model appearance or FPS.

### Native transparent packets and liquid passes

Material evaluation now carries its existing deferred/fixed classification into contiguous draw packets. Opaque-first composite children remain atomic and retain source order; deferred packets enter the native transparent queues. Packets share the native model depth key so explicit M3 material priority is respected before region depth, with deterministic source packet ordering as the final tie break. Opaque packets use scene pass0; transparent packets use pass1/2 according to the native lighting's liquid flags and the animated region bounds. Straddling packets are submitted to both sides when native split rendering is enabled and use opposite camera-space clip planes in the M3 pixel shader. Native hardware clipping is disabled inside the isolated M3 draw and restored afterward. If the native split mode is unavailable, the existing scene/camera-side selection is preserved.

Object fading promotes originally opaque passes to alpha blending and keeps their depth writes enabled so the faded opaque surfaces still occlude one another; authored transparent passes remain depth-write disabled. Material values and packet ranges are frozen once at queue construction for that model draw; they are not re-evaluated separately for each native packet. MaterialPacket is part of the portable core and contains no engine pointers. Shader interface v7 uses c0..c92. No M2Element layout changes or placeholder callback takeovers are needed; existing queue fields carry the M3 packet index, depth, priority and liquid flags.

The Xal'atath `Void_Cape` fallback uses only its exact authored blue-purple gradient and cloak-mask dependencies when the private MADD graph cannot be decoded, so the cape still reaches the standard alpha shader instead of the neutral sheet path. Its triangles are sorted far-to-near by frustum-clipped, screen-area-weighted depth when visible; triangles outside the CPU estimate or degenerate at the camera/near-plane boundary receive a per-triangle conservative depth estimate, so one such triangle cannot disable sorting for the rest of the cape. The GPU remains the sole frustum clipper, and every source triangle stays in the draw. Intersecting transparent triangles still use an average per-triangle depth, so this is not order-independent transparency. Close-zoom appearance still needs in-game visual confirmation.

The queue traces cumulative opaque/transparent/split packet counts and draws per native phase. Material parity and placement/clip fixture checks establish source order, coverage and numeric behavior. Live inter-object sorting and liquid boundaries still require gameplay evidence. Per-triangle transparency, source particles/ribbons, native world shadow passes and unsupported-material diagnostic fallback fidelity remain open; this is not complete SC2 scene-rendering equivalence.

### Explicit emotes and indexed surface submission

Explicit native emote events now enter a module-owned EmoteRequest in addition to the ordinary animation chooser. The M3 stationary movement check permits emotes while stationary and rejects them while moving; the original M2 branch is preserved. Player text emotes and incoming native emote events both feed this request. Persistent Dance survives the placeholder's idle fallback and repeats its mapped source clip. Movement, jumping, combat, casting, stand-state changes, a conflicting chosen action, or explicit StopDance clear it. Other emotes expire after the mapped source duration; a new explicit request can restart the same action. These are local visual states; no unit fields or network packet layouts are modified.

The standard/MADD surface path now uploads the region-local shared vertex array and its original uint16 indices, rather than rebuilding normal/tangent/UV/color data for every triangle index. Regions keep independent palettes and UV seams. A private 65,536-vertex stream supports the full uint16 index range; index submissions retain the 60,000-index chunk limit. Adjacent opaque packets may share one isolated world draw boundary without changing their material order. Transparent and fading packets keep separate sorting. Existing physics, geometry and shading quality are retained.

Native world traces now include packed-vertex/index counts, model queue frequency, and five-second CPU timing windows for animation/physics ticks, packet construction and draw callbacks. This is diagnostic data, not an FPS acceptance claim. Live debugger evidence for the preceding build is saved under verification/m3/dance-fps-live: three of four main-thread snapshots landed in surface normal/tangent transformation, and one in day/night cloud work. New GPU fixtures compare expanded and indexed submission (170 pixel checks); their pass does not prove current-build gameplay performance or Dance behavior.

### Native M3 selection geometry

Native selection broad-phase queries now use an enclosing sphere of the current M3 bounds transformed into the same camera space as the native ray. Registered instances still loading report no selectable geometry; they do not fall back to picking the hidden ErrorCube. The triangle query transforms the ray into source space without normalizing its direction, preserving the native distance parameter under scale, reflection and rotation. It intersects the current skinned, cloth and rigid pose. Native hit owner, group-priority handling, two-pass arbitration and final segment fraction remain owned by CM2Scene.

The portable PickingIndex keeps bounds over blocks of 32 source triangles. GxScene refits it lazily on the first query after a pose change; rendering and animation ticks do not build picking geometry. Subsequent queries for that pose reuse the blocks. Eligibility follows visible source material passes, composite opacity, standard-material NoHitTest (0x40), and the explicit PHCL/PHAC hidden-helper region list. Unrelated material classes' bits are not interpreted as standard flags. Geometry is double-sided like the native triangle query; this does not sample texture alpha holes. Mode1 selects opaque passes, mode2 transparent passes, and ordinary unit-selection modes consider both.

World traces expose pickQueries, pickHits and picking CPU time. The implementation and storage stay in the module. Collision-mode queries use the source data described below. Terrain contacts remain unfinished; source attachment integration is described below. Corpus, synthetic and affine-ray checks establish data/math behavior; actual native owner selection and cursor interaction still require an in-world check.

### Source collision queries

Scene owns decoded explicit collision vertices/faces, the inline SSGS tight volume, and a separate collection of fuzzy SSGS volumes. Decode runs with the source parser, validates references, bone domain, finite dimensions/matrices and triangle indices, and applies a 32 MiB decoded-data budget. A failed decode records an error and disables source collision queries while retaining the renderable scene. World binding logs the failure once with source path and unit identity; it does not substitute ErrorCube collisions.

Explicit model collision triangles take precedence. Otherwise the tight volume follows its source bone's current world pose and its local matrix, including the solved rigid pose. A negative bone disables a tight volume. Fuzzy volumes are preserved as separate source hit volumes and are not promoted to physical blockers. Cuboid half-extents, sphere radius, capsule radius/cylinder-section length and cylinder radius/length follow the local m3studio shape enum and converter interpretation. Shape2 is a capsule, confirmed by the local authoring/drawing implementation; the older schema's uncertain cylinder description is not used as the enum definition. Primitive intersections are analytic, including capsule hemispheres and closed cylinder caps; custom shape4 uses its source triangles. This does not tessellate a sphere or capsule into an approximate collision mesh.

Native CM2Scene camera-space and world-space mode3 ray queries now dispatch to this data and preserve native distances, owner selection and group priorities. Broad-phase spheres enclose the transformed source collision bounds. Collision queries remain active when a model fades out, matching their independence from surface opacity. Error/missing/disabled collision data returns no hit while preserving model rendering. Native M2 collision branches are retained. World traces expose collisionQueries/collisionHits in addition to selection timings.

This supplies ray/segment collision queries, not character-movement collision, a terrain-contact solver or source actor collision-response equivalence. The supplied corpus has eight active tight capsules and 80 fuzzy volumes; the other files have no active tight/explicit collision geometry. Independent parser parity and analytic fixtures do not prove actual host-world callback behavior. In-world query acceptance remains open.

### Source attachment points and native children

Scene owns ATT_ names, source indices/bones/values, addon values, and source bind pivots. The exact 34-name Wrath ID policy is shared by behavior with the existing converter and checked against its table; aliases are not invented for unknown names. A duplicate target ID follows the converter's first accepted source point. Unknown points remain in the catalog with an explicit reason. The supplied corpus has 164 points: 152 mapped and 12 retained without a Wrath ID. Non-default addon/unknown values are retained with warnings; their additional source semantics are not implemented.

World loading indexes the source bytes and reads bounded attachment metadata before recreating aura effects. Geometry, animation and full reference validation remain on the private parser worker. The same owned Document moves to that worker without copying the full file. Scene::FromDocument completes all deferred reference validation before decoding or publishing a renderable Scene. Metadata reads validate every reference they access. Counts/names are capped at 4,096 records/1 MiB.

AttachmentData caches the source bind pivots from IREF. Each world update evaluates mapped attachment frames from the same current pose used for rendering, including solved rigid bones. It publishes immutable native matrices/pivots to a separate registry guarded by a shared mutex. Native animation workers retain a snapshot rather than reading mutable GxScene data. Source frames are converted by basis conjugation for native child-model axes; child transforms use the parent's actual camera-space matrix. World M3 body draws also use that matrix, including native parent attachment transforms.

HasAttachment, pivot/position/scale/world-frame getters, attachment visibility, child animation, frame-of-reference propagation and native model replacement dispatch through the registry for registered M3 parents. Children retain the normal 0xFFFF native index sentinel: source bone indices never enter placeholder attachment arrays. Known points can keep a pending attachment while the M3 loads; pose-dependent children stay hidden until ready. Direct world-frame/scale getters preserve native root fallback, and explicit attach-to-root requests use the source root pose. Zero-scale/noninvertible source frames are unavailable rather than passed into native bone arrays. Deferred world-position offsets are applied when their attachment frame becomes available. Normal M2 branches retain their prior logic.

Unit binding registers before aura/effect recreation, removes snapshots on replacement/destruction, and clears snapshots/requests before world/device teardown. The registry synchronization object has process lifetime to avoid cross-unit C++ static destruction ordering; world reset releases the data. Native dispatch counters are process-wide and labeled attachmentReadsAll, attachmentPublishesAll, attachedChildrenAll and attachedPositionsAll. They are not per-unit counts or a performance acceptance result.

Independent parser/converter metadata parity, all-clip source pose checks, and a four-reader immutable-snapshot fixture cover the data/math/lifecycle pieces. These do not prove actual native equipment/effect orientation, pending world-offset appearance, mounted placement or gameplay FPS. Those require an in-world check. Source event/VFX attachment semantics and a fresh full-host integration/transfer remain open.
