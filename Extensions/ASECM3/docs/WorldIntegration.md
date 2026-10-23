# World instance adapter (in progress)

## Current status, 2026-10-16

The player M3 override has been observed in game; user feedback on installed
E04 reports that cloth is much better. This is not a DBC-selected nonplayer test
or a new FPS acceptance. Current source contains native world-light/fog inputs,
transparent/water-plane packet placement, module bounds/picking/collision-query
bridges and immutable attachment snapshots. Early sections below describe their
initial integration state and must not be read as a current completion audit.
World shadows/depth behavior, device recovery, source-complete materials/VFX/
animation layering and source terrain/contact/actor physics remain incomplete.

### DBC selection and provenance

DBCModelQuery.md describes the module-owned read-only loaded-table query and
Player inspection control. This can verify the effective display/model name
without assuming the loose source table won the archive-priority resolution.

The existing GetModelFileName path returns CreatureModelData.m_modelName for
any unit without an active Player override. UpdateDisplayInfo passes that path
to PrepareUnitModel before native model creation. A .m3 suffix selects the same
module instance path for NPCs and players; ordinary M2/MDX paths remain native.
This dispatch is present in installed E04, but the actual nonplayer DBC route
still needs an observed live check.

The current source candidate adds selection=dbc/override, objectType, displayId
and modelId to world traces and UnitWorldInfo. The DBC branch is identified by
the original string pointer returned by this host's GetModelFileName, before
copying the filename. An equal-text Player override remains an override. IDs
are snapshots; no DBC pointer or server field is retained or modified. Reusing
the same source model refreshes provenance when display selection changes.
On another host that copies the DBC filename before dispatch, adapt this
diagnostic contract to carry explicit provenance. It does not affect rendering.
These diagnostic additions are included in the context-deferral client update.
The actual DBC-selected nonplayer test remains pending; preparation and origin
logging do not establish that it has occurred. See DeviceLifecycle.md for the
graphics-context deferral contract and its remaining validation limits.

On 2026-10-17 the existing custom gallery model70001/display300001 was changed
from its Alexstrasza gallery M2 to HOTS_M3\alexstrasza_base\alexstrasza_base.m3.
The original CreatureModelData table is backed up under client
ASECM3/Backups/20261017-000640-dbc-model70001/CreatureModelData.dbc.
Original SHA256091c55089c886a39bd8744b4620bf18870499e4656f124227a381d3d07cb30f1;
installed SHA2568ea42a0952d4abd4ca34990588965b5fb6889894efb76056234324034a5f09f1.
Only five existing bytes in the string-size/name-offset words differ; a45byte
string is appended. All15121records and the CreatureDisplayInfo table remain.
The native loaded-table query confirms the effective M3 path/existence, and
the exact virtual path passes native preview drawing with its relative
RequiredAnims bank. This still is not an observed DBC-selected live-unit draw.
The client-data table is separate from portable module/runtime asset exports.

### Staging a DBC entry

tools/stage_dbc_model.py creates a separate CreatureModelData.dbc with one
explicit existing record's model-name offset changed and a new string appended.
It requires a fresh output folder, rejects wrong schema/truncation/duplicate
record IDs/invalid strings/absolute or parent-traversing model paths, and verifies
every original byte outside the header string-size and selected name-offset
words. Input hashes are rechecked after staging. No input file is edited and
no server or CreatureDisplayInfo records are changed.

Example (run from the workspace):

```powershell
python WoW335_ASEC/wowdecomp/Modules/ASECM3/tools/stage_dbc_model.py `
  --source-dbc G:/Client335/Data/DBFilesClient/CreatureModelData.dbc `
  --display-dbc G:/Client335/Data/DBFilesClient/CreatureDisplayInfo.dbc `
  --model-id 70001 `
  --model-path HOTS_M3/alexstrasza_base/alexstrasza_base.m3 `
  --output-directory WoW335_ASEC/verification/m3/dbc-alexstrasza-70001-staged
```

The staged example uses the existing gallery Alexstrasza entry. Its sole
CreatureDisplayInfo reference in the inspected loose table is display300001.
All display records sharing a model ID share its replacement. The report lists
those IDs when a display table is provided. Installing this staged table is a
separate operation requiring a backup and client restart; this helper does not
install it. Do not bundle these client-data tables in the portable module kit.

The inspected loose Data/DBFilesClient snapshot has15121model records and
92423display records, with no .m3 model-name entries. This does not prove which
table a running client's mounted archive priority actually selected. The target
M3 exists in Data/Patch-M.mpq/HOTS_M3, but resolving its relative virtual path
and drawing a nonplayer with selection=dbc still require a live test.

## Implemented module API

`client/WorldInstance.cpp` owns one GxScene per world object, keeping all
source data, animation, material, cloth and rigid state inside module objects.
WorldInstance takes an owning Scene plus the source path; host-owned unit/DBC
pointers are not retained. Its Advance method accepts delta time and the native
texture provider. Semantic action changes reuse ResolveActions/SelectAction.
Repeated action IDs do not restart their clips, and invalid IDs are rejected.

The world Draw method takes the current model-to-camera affine matrix and the
engine projection matrix (not an already converted native projection). Positions
are transformed to camera space once. Normals use inverse transpose; tangents
use the forward linear transform and orthogonalization, with reflection-aware
handedness/culling. Shader and diagnostic fallback paths both use this transform.
No preview fit, grid, eye offset, source-unit guess, viewport override or scene
clear is introduced. The host's current color/depth targets remain in place.
The caller must include the WoW camera-relative world origin in modelToCamera.

Bounds come from currently visible skinned/physics-updated region vertices and
are transformed through all eight corners for world-space culling bounds. Call
Advance after pose/visibility changes before querying Bounds. Explicit PHCL/PHAC
helpers stay excluded. Source/transform nonfinites and singular/nonaffine matrices
are rejected rather than submitted to the device.

`client/GxRenderGuard.cpp` now owns the existing full D3D9 state block, explicit
shader-bank restoration, Gx shadow/cache restoration and private stream buffers.
The old asset viewer header keeps compatibility aliases; it has no duplicate
implementation. A WorldRenderContext shares streaming buffers across instances
and must be destroyed on the render thread before graphics teardown. Restoration
failures stop that context. Existing preview isolation remains unchanged.

## Unit filename dispatch (experimental)

UnitWorldBinding.cpp intercepts CGUnit_C::UpdateDisplayInfo after the native
filename lookup. M3 paths from CreatureModelData or the Player visual override
are dispatched to module-owned instances. A hidden Spells/ErrorCube M2 provides
native unit object lifetime, transform and lighting registration.
No DBC bytes or server display fields are changed. Original M2/MDX paths keep the
native implementation. This currently covers units, not every game object class.

The module snapshots model/bank bytes through SFile on the client thread; a
single private worker parses owned bytes and compatible animation banks. Limits
are 128 MiB per dependency, 256 MiB queued snapshot bytes, 64 distinct live source
jobs and 512 bindings. Archive reads themselves are still synchronous. Units
share immutable source data; animation, cloth, textures and runtime state remain
per instance. GUID plus live object identity is revalidated at callback time.
Unit destruction/replacement removes registry entries; world destruction releases
bindings, joins the parser and releases streams before graphics teardown.

PlayBaseAnimation forwards the requested WoW action before the M2 fallback.
Repeated actions retain the current clock; unmapped actions retain their source
clip and are exposed in Player diagnostics. Animation/cloth update from MovementIdleMoveUnits, including offscreen units. The module draws through a dedicated native queue entry using the unit model's
world transform and scene view. Player reports
loading/ready/draws/state failures. Logs/ASECM3-world.log records draws 1/120/600
with source, bank, action and render-boundary counters. Critical errors also use
Logs/ASEC-errors.log. These counters prove submission, not pixel fidelity.

## Remaining world work

The current dedicated queue entry submits all M3 material passes in the opaque slot;
transparent global sorting and water-plane separation are not integrated.
Culling still uses the hidden M2 bounds, not the module's computed visible
bounds. Shadows/depth-only passes, attachments, hit tests, world lighting/fog and
terrain physics remain required. Rigid world motion/terrain coupling is still incomplete. Current lighting is the module preview approximation
in camera coordinates. Physics uses the local model contact plane, not game
terrain. The compiled unit bridge and Player route require actual logged-in
runtime/visual validation; no working world-avatar claim follows from Glue.

## Evidence

world-transform-inspection.json checks point/normal/tangent/reflection math and
rejection of singular/nonfinite/nonaffine transforms. It does not exercise world
draws, object dispatch, bounds against real world objects, lighting or avatars.
The native viewer regression is recorded separately to cover the shared guard
move, not to prove world integration. The full M3 objective remains active.

## Player-name callback collision correction

The first logged-in attempt displayed ErrorCube instead of M3. Source inspection
confirmed PlayerNameUpdateDrawCallback and PlayerNameDelete replace/clear the
same m_drawCallback slot previously used by the adapter. The adapter now leaves
that slot and its argument untouched. CM2Scene queues a module-owned type-6
entry separately from native name type-5 callbacks; CM2SceneRender dispatches
type 6 through the guarded module renderer. Registered proxies submit no native
geometry batches even if later load/geoset callbacks re-enable their sections.
Queue sorting handles type 6 through the existing non-geometry default path.
Attached/queued/draw/error phases are logged independently, including before
source parse completion. The failed live attempt contradicts working-world
claims for the previous build; this correction still needs logged-in validation.

## Facing, native action selection and world cloth inertia

The first world display was confirmed by the user and 600 actual draw/guard
boundaries with zero failures. User acceptance then identified +90 facing error,
Stand-only playback and no cape response to translation. These are failures of
that installed build, not verified fidelity.

The next build applies the converter's established +90 Z source basis (HotS -Y
forward to Wrath +X) before the unit model/world/view matrix. Source bones and
physics remain in their original frame; the complete rendered result rotates.
MovementIdleMoveUnits calls the module tick after native movement, regardless of
visibility. It queries the native ChooseAnimation priority rules before the
placeholder's sequence fallback, then applies the module semantic mapping.
ChooseMovementAnim accepts a module-owned skeleton in place of the placeholder's
missing native bone readiness bits; all movement masks/blocked states remain.
Invalid/sentinel action IDs do not replace a valid action. Per-frame rendering
loads at most one texture per unit and does not advance animation a second time.

Cloth particles, previous anchor targets and velocities rebase from the prior
source-to-world transform into the new object frame. The source mesh, constraints
and animation remain unchanged. Translation and turning now affect cloth inertia;
teleports above 32 source-local units per update and scale changes reseed cloth.
Stationary preview never supplies a world transform and keeps prior behavior.
This is the module's solver policy, not proprietary Domino/source coefficient
parity. Rigid-body world-motion coupling, nonvertical gravity and terrain contact
are still incomplete. Cloth-world-motion-inspection.json exercises all four
cloth-bearing Alexstrasza variants with finite movement response, exact pinned
vertex invariance and teleport reseeding. Visual appearance remains a live gate.
