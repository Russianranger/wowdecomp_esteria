# Handoff: Retail race customization

Current state: all requested race slots are enabled in the installed native Retail client, with both
sexes. **The roster expansion is installed; live acceptance is pending.** Human armor/helmets/emotes
and restored Dwarf heads were previously accepted by the user. Server phases 2-3 (full Retail-choice
persistence) remain unimplemented. Earlier work was committed separately; the current follow-up is
uncommitted. See [retail-race-customization-plan.md](retail-race-customization-plan.md).

Repos: client `R:\Users\Zach\Documents\GitHub\wowdecomp` (branch `Esteria`), server
`R:\Users\Zach\Documents\GitHub\EsteriaWoW`. Dev client: `G:\3.3.5a - Dev`.

## Latest fixes: Elf hands, Orc tattoos/posture, Blood Elf blindfolds (2026-10-19)

Installed executable SHA-256 **`f5b260430904095a7bfb9550d0defdbb35ee26490eaedc0754cf9fda2ee5a405`**. Fresh launch with the same
`G:\3.3.5a - Dev\LaunchRetailRaces.cmd` (the Human launcher is also still equivalent).
These repairs are **installed and offline validated; live acceptance is pending**.

- **Missing Elf hands:** original Night Elf, Blood Elf and Void Elf male/female SKINs have separate
  palm/finger geoset 2301. The high-group reset hid it; none of their default recipes selected group23.
  `DefaultRaceGeosets` restores 2301 only when the recipe omits that group. Explicit 2300/2301/2302
  choices retain priority. Original bounds show cuff401-404 meshes stop above the hand, so cuffs
  cannot substitute for it. All49 original roster bodies were inventoried; only these six have2301.
  Evidence: `G:\RetroportWork\retail-roster-20261019\hands-anatomy-audit.json`.
- **Orc tattoos:** ChrModelTextureLayer's character compositor blend enum was interpreted as M2
  material blending. Orc tattoo target16 uses mode4, meaning multiply. Its opaque white background
  must preserve skin, whereas the old additive interpretation brightened the whole sheet. The
  compositor now follows the authored copy, multiply, overlay, screen and alpha operations, keeping
  legacy equipment alpha-over separate. Both sexes' three tattoo patterns darken only marked
  pixels, with untouched pixels unchanged. Primary reference:
  [wow.export shader](https://github.com/Kruithne/wow.export/blob/c2fd7bde36a712be78a5da896c995b84fbfa2545/src/shaders/char.fragment.shader),
  [blend state](https://github.com/Kruithne/wow.export/blob/c2fd7bde36a712be78a5da896c995b84fbfa2545/src/js/3D/renderers/CharMaterialRenderer.js).
- **Orc/Mag'har male Upright/Posture:** source choices439/3427 reference conditional-model rows1/2,
  not BONE morphs. Both rows resolve through CreatureModelData10882/10883 to native M2 **1968587**;
  hunched choices438/3426 return to917116. Added pinned table3460182, layout601A4522, and scoped
  conditional resolution to these two male races. Runtime replacement keeps the component,
  preferences, equipment, transient choices and scene placement; updates registered world owners;
  moves attachments with temporary references; and releases the previous component reference last.
  Owner registration is cleared even when logout retains the active component. Loaded callbacks
  resolve torso keys; self-referential frame matrices are rebased to the new instance. Model swapping
  leaves texture requests on their normal lifecycle. Original skeletons/assets remain immutable.
- **Blood Elf blindfolds:** current selected cloth is a skinned collection using texturetype9. Its
  materialtarget40 is gated to Demon Hunter eye choices7641/62819 from another ChrModel; normal eye
  colors previously yielded no cloth texture. A visible blindfold with no compatible target40
  material now gets the first authored palette (green glow) as a bounded visual fallback. Explicit
  compatible green/blue choices win, and the selected eye texture is unchanged. The None choice
  remains hidden and adds no cloth material; ordinary hair/color dependencies retain their filters.

Validation: Release x64 client build and `retail-assets` CTest passed. **98 compositions** cover60
default race/sex/alias looks, eight Orc tattoo settings, four posture mappings, all24 Blood Elf
blindfold settings and two explicit palette branches. Pixel checks prove tattoo locality/darkening
and that blindfold selection preserves normal eyes. Both blindfold collection rigs match the body;
the upright body's eight streamed chat/laugh/dance clips and both legacy helmet donor anchors pass.
The full newly authored body-model swap still needs creator/world testing; offline acquisition and
recipe checks do not establish live render/refcount/animation acceptance.

**Persistence remains unchanged:** Upright is an extra retail choice, so it can still reset after
creation/relog where the existing server transport does not store it. This repair makes the actual
toggle display the authored posture. Other conditional forms, BONE morphs, particle customization
and full extra-choice persistence remain separate work.

Source build remains `88d5e8ef05ec5a8446cdec93d142d7c6`. Raw conditional DB2, upright source M2,
probe logs, before/after textures, regression receipts and installer:
`G:\RetroportWork\retail-roster-fixes-20261019`.
Install receipt: `install-receipt.json`. Previous executable/metadata backup:
`G:\3.3.5a - Dev\_backups\retail-roster-fixes-20261019-085352`. To roll back this repair with the game closed, restore both backed-up
files to the paths listed in this receipt. The roster/Glue installation below is still the baseline.

Next live checks: Elf male/female hands bare and with gloves; Orc male/female tattoo None/patterns;
Orc and Mag'har male posture toggled both ways, with armor/weapons/helmets and animations; Blood Elf
male/female blindfold None plus all styles, alongside normal eye colors. Check return from preview,
race/sex switching and logout while a posture is selected. Keep `Logs\RetailRace.log` for any issue.

## Prior install: full requested Retail roster (2026-10-19)

Fresh launch using **`G:\3.3.5a - Dev\LaunchRetailRaces.cmd`**. The existing
`LaunchRetailHumans.cmd` launches the same expanded roster. Both preserve argument forwarding, the
Vulkan executable and the online research config override. First uncached assets can download.

Canonical target/source identity mapping: [RetailRaces.csv](../Tools/RetailAssets/RetailRaces.csv), also
installed as `Data\Retail\RetailRaces.csv`. These are existing effective client race slots; no new
RaceIDs, server allocations, DBC replacements or SQL migrations were made. Target Pandaren 18 maps
to source Alliance 25; registry-only target 26 has no effective models and is deliberately omitted.
Highmountain is source 28, Zandalari source 31. Both faction slots for Earthen and Haranir are enabled.
Kul Tiran target 29 has effective client models despite being absent from the server registry.

```text
WOW_RETAIL_RACES=1:1,2:2,3:3,4:4,5:5,6:6,7:7,8:8,9:9,10:10,11:11,12:22,17:27,18:25,19:29,20:35,21:30,22:31,23:34,28:70,29:32,45:36,46:28,47:37,48:84,49:85,50:86,51:91
```

Shared native behavior now includes Retail bodies, geosets, composed textures and legacy armor;
full creator option enumeration; derived legacy helmet placement; streamed chat/laugh/dance; and
the existing Human/Dwarf full-head and closed-boot-foot corrections. Boot substitution is restricted
to compatible humanlike races; it preserves authored hooves, mechanical feet and separate Feet choices.

Additional integration needed by the expanded roster:

- Customization collections are root-attached, retain their own matrix storage and copy matched body
  skin matrices by unique bone CRC/ancestry. Their selected geosets and composed texture targets are
  applied after asynchronous load. Intermediate pruned joints are allowed; invalid rigs stay hidden.
  Registrations are cleared on detach/free, and ordinary attachments keep their existing behavior.
- Explicit player/collection registration permits the existing legacy material approximation for
  embedded-bone bodies such as Haranir. Generic unsupported shader validation remains strict.
- SKPD skeleton parents are fetched recursively with cycle/depth checks. Flattening preserves child
  sequence overrides and parent motion payload ownership, including parent aliases whose targets are
  overridden by the child. This repairs Mechagnome inherited emotes. Dracthyr's global rig can exceed
  256 bones; section-local byte palettes remain bounded while global indices use uint16.
- Helmet fitting uses original stock race crown conventions or the effective custom model donor.
  High RaceIDs use bounded visual helmet-mask aliases, independently of faction eligibility (Horde
  Earthen uses Dwarf anatomy, Horde Haranir uses Night Elf anatomy). Legacy helmet instances now bind
  to an explicit head-bone frame with the donor crown offset. This avoids extra scale/rotation on
  Retail-only helper bones (notably Dracthyr, Earthen and female Zandalari) while following every head
  pose. The accepted Human/Dwarf transform is algebraically identical for their identity helpers.
  Frame state is instance-local, checked against the parent model identity and cleared on detach/free;
  the original model skeleton and socket are unchanged for other attachments and Retail helmets.
- Frozen saved-choice IDs retain the installed Vulpera, Highmountain, Earthen, Haranir and Mechagnome
  codecs. The generator input and codec provenance are in the external evidence directory. Current
  Retail DB2 order must never be substituted for an old packed choice order. New IDs that cannot fit
  the existing codec remain transient preview choices and cannot overwrite neighboring packed digits.
- GlueXML capacity increases from 29 to 64 controls and queries the actual option count (Dracthyr 45).
  The root and locale winning CharacterCreate.lua entries were patched together and read back.

Offline evidence: **49 unique bodies, 58 race/sex default compositions, 9,563 individual-choice
recipes, 12 collection rig bindings, 297 external emote clips and 56 helmet donor/target head frames
passed**. Individual-choice recipes vary one option against the other defaults; this is not a full
cross-product or unlock/requirements test. Original SKIN inventory confirms full head 3202 on all 49
bodies. Body atlas contact-sheet review includes intact faces. The collection surface audit also
passed: 376 requested mesh selections exist in the original SKINs;
one Dracthyr selector is the source's explicit signed -1 no-mesh action (rows 497/614), documented in
`collection-none-selectors.log`. This does not require inventing an absent section 1399.
Lua 5.1 syntax/idempotence and the `retail-assets` CTest passed; regressions cover frozen codec
neighbors, large global bone indices,
inherited/overridden animation owners, head defaults and boot selection. Client Release x64 build passed.

**Remaining behavior limits:** full Retail-choice server persistence is still limited to the five
stock bytes or each race's existing packed codec. Additional new choices can reset on relog. Bone-set
actions (e.g. posture/body shape), conditional replacement models, particle customization and unlock
requirements are not implemented by this native appearance resolver. The default body is audited;
alternative forms such as Dracthyr visage are not newly integrated by this change. No live creator,
world, remote-player or equipment acceptance is claimed for this install.

Source remains Retail 12.1.5.70448, build `88d5e8ef05ec5a8446cdec93d142d7c6`. Disk exhaustion on R:
caused misleading CASC fetch errors. The existing cache was copied with per-file SHA-256/size checks
to `G:\RetroportWork\retail-casc-cache-88d5e8ef05ec5a8446cdec93d142d7c6`, then the research INI
was pointed there. The old cache was removed only after verification. Acquisition stayed serial to
avoid concurrent CASC archive writes. Details: `cache-relocation-receipt.json`, `casc-fetch-diagnosis.json`.

Installed executable SHA-256: `19cb5ae1741122515ca629497cd5d0a52951d4e37835a6cb3abcc74e1bb5dc01`.
Backup of prior executable, metadata and launcher: `G:\3.3.5a - Dev\_backups\retail-roster-20261019-080107`.
Glue archive backups: `G:\3.3.5a - Dev\_backups\retail-glue-20261019-080121`.
Full receipt, per-file hashes, audit logs, original inputs, codecs, contact sheet and reproducible
validation/install scripts: `G:\RetroportWork\retail-roster-20261019` (`install-receipt.json`).

Rollback with the game closed: restore paths marked `existed` in the receipt's `rollback` list from
their verified backup paths, remove newly added paths marked false, and restore both root/locale
MPQs from the Glue backup. Restore the full matched set, not just the executable.

Next live checks: select each race and sex in a fresh client; vary face, hair and attached accessories;
inspect boots and helmets front/side; remove/re-equip armor and robes; chat, `/lol`, `/dance`, run,
mount and attack. Revisit Human/Dwarf after modern races and verify save/relog behavior for existing
packed appearances. Collect `Logs\RetailRace.log` plus a screenshot for any remaining defect.

## Prior install: closed feet under legacy boots (2026-10-19)

The user confirms the Dwarf head repair works, but sees individual toes through boots on both Humans
and Dwarves. The boot texture was painted on the bare-foot mesh, rather than toes intersecting a
separate boot model. `RetailRaceEquipment` previously chose 2001 for style-zero boots or robes and
otherwise used `2001 + Wrath shin style`. Wrath group 5 shin style is not Retail group 20 foot style.
The visible creator starter boots, display 10141, have shin style zero.

Original Human/Dwarf male/female meshes were compared: 2001 has separate toes; 2002 has a closed shoe
silhouette. All four models supply both variants. Evidence and an orthographic mesh preview are outside
Git: `G:\RetroportWork\retail-dwarf-20261019\foot-variants.json` and `foot-variants.png`.

`RaceGeometry.h::ApplyLegacyFootGeometry` now hides all group 20 alternatives and selects 2002 whenever
legacy boots are equipped, or 2001 when barefoot. The loaded-skin availability check remains, with a
bare-foot fallback for models lacking 2002. `RetailRaceEquipment` no longer takes a skirt parameter:
robe/dress coverage still selects the stock shins, but cannot restore bare toes under equipped boots.
The hook remains scoped to active mapped Retail player components and runs after stock geoset prep.

Client build and `retail-assets` CTest passed. Visibility regression cases cover actual style-zero
starter boots, swapping to styled boots, repeated geometry refresh, removal/re-equip and missing-mesh
fallback, verifying that exactly one foot variant remains visible each time.

Installed `G:\3.3.5a - Dev\WowClientRetailVulkan.exe`, SHA-256
`8a9abe6c5cc01b397ac6dfb7fc6f16c29e255b9683cfa1f4f220b7494e6065b1`.
Use the same `LaunchRetailHumans.cmd` (`1:1,3:3`). Backup of the accepted head-fix executable/metadata:
`G:\3.3.5a - Dev\_backups\retail-player-boots-20261019-063650`.
Receipt: `G:\RetroportWork\retail-dwarf-20261019\boot-fix-install-receipt.json`.
Next live checks: fresh launch, Human/Dwarf male/female boots in creator/world, remove/re-equip, swap
boots and add/remove a robe. Confirm bare toes return only when barefoot. Other animation/helmet
checks remain useful; this repair changes only the legacy boot foot-selection rule.

## Prior install: restore invisible Dwarf heads (2026-10-19)

The first Dwarf install rendered the body, hair, eyes and options, but hid the face/head for both
sexes (user screenshots). This was a visibility-default bug, not missing face texture downloads.
The earlier asset/recipe checks did not exercise the final head visibility state.

`RetailRaceGeometry` hides geosets 2001-9999 before applying recipes and anatomy defaults. Human's
Face Shape choices explicitly select group 32 (3202/3203/3204). Dwarves have no Face Shape option:
their Face choices change textures and never select group 32. The default list restored feet, torso
and eyes but omitted the head, so the existing full Dwarf head mesh remained hidden.

Original SKIN section/material/bounds comparison is saved outside Git at
`G:\RetroportWork\retail-dwarf-20261019\surface-comparison.json`, generated by `compare_surfaces.py`.
Male Dwarf full head: section 125, geoset 3202, Z 1.2592-1.5582, body texture type 1.
Female Dwarf full head: section 91, geoset 3202, Z 1.2457-1.5053, body texture type 1.
3201 is the neck-only variant on both, so enabling it would not restore the face.

`Engine/Source/Retail/RaceGeometry.h::DefaultRaceGeosets` now supplies 3202 alongside the previous
anatomy defaults only when the recipe leaves that group unspecified. `RetailRaceGeometry` uses
that shared selection. Explicit Human face shapes, including hidden-group values, take precedence.
Regression tests reproduce both pinned Dwarf recipes and check Human 3200/3202/3203/3204 overrides,
plus retention of the prior anatomy defaults. Client build and `retail-assets` CTest passed.

Installed `G:\3.3.5a - Dev\WowClientRetailVulkan.exe`, SHA-256
`640e66f15c6d3a3ed08ff7bb6d65a03b46d5f74095d42152defb0fcbacba0cd9`.
Use the same `LaunchRetailHumans.cmd` (`1:1,3:3`); no launcher/archive/data changes were needed.
Previous executable/metadata backup:
`G:\3.3.5a - Dev\_backups\retail-dwarf-heads-20261019-063145`.
Receipt: `G:\RetroportWork\retail-dwarf-20261019\head-fix-install-receipt.json`.
**The user confirmed the restored Dwarf heads work.** See the newer boot-toe correction above.

## Prior install: retail Dwarf players (2026-10-19)

The user confirmed the Human animation repair works and requested retail Dwarves. The same native
streaming path now maps original Wrath RaceID 3 to retail RaceID 3, alongside Human `1:1`.
No new server race identity, DBC allocation or SQL is required for this existing playable race.

Pinned source remains retail build `88d5e8ef05ec5a8446cdec93d142d7c6` (12.1.5.70448).
Actual model SKID chunks identify the shared skeletons below; do not substitute the recipe's nominal
`skeletonFileDataId` (4690407/4690406) for these SKIDs.

| Sex | ChrModel | M2 FileDataID | SKEL FileDataID | First SKIN | Layout | Options |
| --- | --- | --- | --- | --- | --- | --- |
| Male | 5 | 878772 | 1892675 | 878823 | 107 | 14 |
| Female | 6 | 950080 | 1892673 | 950113 | 108 | 11 |

- The existing recipe/body/equipment/animation paths handle both models. Original meshes include
  foot variants 2001-2008 and base groups 0/2201/3301/5101. Both default 2048x1024 body sheets plus
  hair/eye sheets composed successfully and were visually inspected as texture atlases.
- `FitHumanHelmet` is now `FitLegacyHelmet`, scoped to matching mapped Human/Dwarf race IDs. Donors
  are cached by full model path so switching races cannot reuse another race's calibration. The
  effective installed Dwarf bodies come from `Data\patch-Z.MPQ`. Target head/socket checks remain:
  helper directly parented to the head, no translation/scale animation, no nonidentity rotation.
  Male Dwarf's helper has two identical global identity quaternion keys; the new guard accepts those
  resident keys while rejecting per-sequence/rotating socket tracks. Regression tests cover the packed
  quaternion cases. No shared skeleton or original asset is changed.
- Actual Dwarf helmet translations computed from loaded retail and effective installed donor anchors:
  male `(0.040488, 0, 0.213443)`, female `(-0.008967, 0, 0.200279)`. These are derived at runtime.
  Helmet fitting now logs `helmet fit race ... sex ...`.
- Female Dwarf maps the stock facial appearance byte to `Earrings`; Human female remains `Makeup`,
  and males use `Beard`. Full creator controls enumerate the retail options by model as before.
- `RetailRaceProbe <ini> audit <race> <sex>` varies each choice against the other default choices.
  Dwarf male: 195 recipes; female: 169. All 364 resolved, with no missing material mappings and no
  attached-model dependencies. This is not an exhaustive cross-product or unlock/requirements audit.
- Both runtime model/skin imports passed. Thirteen real streamed animation files passed validation
  with changing skeletal keys: male `60,69:0,69:1,69:2,69:3,70,123`; female `60,69:0,69:1,69:2,70,123`.
  Client and tools built successfully; `retail-assets` CTest passed.

Initial Dwarf install `G:\3.3.5a - Dev\WowClientRetailVulkan.exe`, SHA-256
`46127acea9d3513ba79f31696bf6203891efe4dcd22d32c4981c6b91f7dd57ca`.
The existing `LaunchRetailHumans.cmd` now sets **`WOW_RETAIL_RACES=1:1,3:3`**, retaining the same online
research config override and renderer/argument forwarding. Use this launcher for Dwarves too; the
client's installed files-only CASC config alone does not supply these race assets.

Backup of the accepted Human executable, metadata and previous launcher:
`G:\3.3.5a - Dev\_backups\retail-dwarves-20261019-062631`.
Install receipt, input/art hashes, original extracted M2/SKEL/SKINs, effective donor models and texture
atlases: `G:\RetroportWork\retail-dwarf-20261019\install-receipt.json` and its containing directory.
Rollback with the client closed: restore all three backed-up files to their original names.

Next live checks: fresh launch, Dwarf male/female creator and character selection, log in and inspect
armor/boots/robes and helmets from front/side, remove/re-equip, chat, `/lol`, `/dance`, run/mount/attack.
Recheck a Human after a Dwarf to exercise the separate helmet donor cache; NPC scoping remains unchanged.
First uncached choices can trigger CDN downloads. **Full-choice server persistence remains unimplemented**:
the five stock appearance bytes persist; extra creator selections still reset on relog, as with Human.

## Prior install: streamed Human emotes (2026-10-19)

The user confirmed the helmet repair looks correct, and tested working run/mount/attack. Chat talking,
`/lol` and `/dance` did not move the characters after their CDN download.
**The user has confirmed that animations now load and play correctly in-game.** The install below
predates the Dwarf expansion above.

The exact failure was reproduced from `Logs\RetailAssets.log`: female animation IDs 60, 69, 70 and 123
were rejected as out-of-bounds/unsorted external tracks. The files use independent animation chunks:
AFM2 for model tracks and AFSB for shared-skeleton bone tracks (AFSA is supported for skeleton attachment
tracks). Female talk/laugh are AFSB-only; dance and male talk contain AFM2 followed by AFSB. The previous
adapter either read an AFSB header as timestamp data, or unwrapped AFM2 and discarded the AFSB payload.
Embedded movement/combat tracks therefore worked while downloaded emotes could not load.

- `RetailModel.{h,cpp}` adds checked `InspectAnimation`, preserves split payloads in `StripAnimationChunk`,
  and validates each external track against the corresponding payload in `ValidateAnimation`.
- `Engine/Source/Model2/M2Shared.cpp` uses those same payload bases during actual external-sequence
  relocation, with scoped bone/attachment overrides restored before other track categories. The extra
  interpretation is limited to `retail/` models. Original disk/CDN data is unchanged.
- `RetailAssetTests` covers mixed AFM2/AFSB/AFSA files with deliberately overlapping offsets, AFSB-only
  files, missing bone data, malformed/duplicate chunks and skeletal timestamp ordering. CTest passed.
- `RetailAssetProbe --animation` accepts comma-separated IDs/variations and checks the actual runtime
  source path plus changing skeletal keyframes. Both Human models and all 14 requested files passed:
  male `60,69:0,69:1,69:2,69:3,69:4,70,123`; female `60,69:0,69:1,69:2,70,123`.
  Male clips had 108-132 bones with changing keys; female clips had 50-118. This is offline motion-data
  evidence, not a claim of observed in-world playback.

```powershell
rtk proxy build\retail-satchel\Release\RetailAssetProbe.exe build\retail-research\RetailCasc.ini retail/1011653.m2 --animation 60,69:0,69:1,69:2,69:3,69:4,70,123
rtk proxy build\retail-satchel\Release\RetailAssetProbe.exe build\retail-research\RetailCasc.ini retail/1000764.m2 --animation 60,69:0,69:1,69:2,70,123
```

Human animation install: `G:\3.3.5a - Dev\WowClientRetailVulkan.exe`, SHA-256
`a9b5c4c1baa30a4ff731ecf7539fa84f2e97ccb4c8813ee7e3f4e21a98a43b21`.
Use the same `LaunchRetailHumans.cmd`. Previous accepted body/armor/helmet build is backed up at
`G:\3.3.5a - Dev\_backups\retail-human-animations-20261019-055435`.
Receipt: `G:\RetroportWork\retail-human-ingame-20261019\animation-install-receipt.json`.
Next live check: fresh launch, send ordinary chat, `/lol`, `/dance` and repeat after the first download;
test both sexes and recheck running/mounting/attacking. If anything remains frozen, capture the latest
`Logs\RetailAssets.log` and the exact emote/sex. Server phases 2-3 remain unimplemented.

## Prior install: Human helmet placement (2026-10-19)

The user confirmed that the retail bodies and other armor look correct in-world, but showed a helmet
inside the male Human's upper chest. **The user has since confirmed that the helmet correction looks
correct.** The install below predates the streamed-emote correction above.

Cause: the installed Wrath Human helmet meshes use a crown socket. The retail skeleton's attachment 11
is at its head pivot. Both skeletons use an unanimated helper directly parented to head key-bone 6, so
their local frames can be compared without changing the animation or mesh. Effective legacy Human
models were read from `Data\patch-Z.MPQ`; sample installed helmet models come from `patch-G.mpq`.

`FitHumanHelmet` in `RetailRace.cpp` reads each sex's effective legacy Human model through SFile,
preserves its socket-to-head-pivot vector, and derives an absolute local transform for the attached
legacy helmet on the retail body. The retail head animation still drives the attachment. It runs after
equipment geometry and retries delayed parent loads in `RetailRaceTick`, including glue screens.
Only mapped retail Human player components receive this correction. There is no fixed world-space lift,
no shared skeleton mutation, and no cumulative offset. Existing retail helmet models are excluded.

Actual measured local translations for this source/target pair:

| Sex | X | Y | Z |
| --- | --- | --- | --- |
| Male | 0.062556 | 0.000942 | 0.184132 |
| Female | 0.085087 | 0.000000 | 0.185118 |

The executable computes these from the installed donor and the loaded retail skeleton; they are not
hardcoded. `RetailModel.{h,cpp}` adds checked `ReadHelmetAnchor` and `HelmetTranslation` helpers.
Regression tests cover retargeting all axes, identical skeletons, source preservation, missing sockets/
head bones and nonfinite values. The client build and `retail-assets` CTest passed. Actual prepared male
and female models, with their original shared skeletons and skins, passed these probes:

```powershell
rtk proxy build\retail-satchel\Release\RetailAssetProbe.exe build\retail-research\RetailCasc.ini retail/1011653.m2 --helmet-donor G:\RetroportWork\retail-human-ingame-20261019\human-male-target-effective.m2
rtk proxy build\retail-satchel\Release\RetailAssetProbe.exe build\retail-research\RetailCasc.ini retail/1000764.m2 --helmet-donor G:\RetroportWork\retail-human-ingame-20261019\human-female-target-effective.m2
```

Current installed executable: `G:\3.3.5a - Dev\WowClientRetailVulkan.exe`, SHA-256
`5e20be5bb5f5127e428649cead827dfaa6f0402a6bcb704bb1ef074fc4bb820c`.
Use the same `LaunchRetailHumans.cmd`. Backup of the previous accepted-body/armor build:
`G:\3.3.5a - Dev\_backups\retail-human-helmets-20261019-053522`.
Receipt: `G:\RetroportWork\retail-human-ingame-20261019\helmet-install-receipt.json`.
The next launch logs `helmet fit sex ... display ... model ... donor ... local shift ...` in
`Logs\RetailRace.log`. Verify the reported helmet from front/side while idle, running and turning the
head; remove/re-equip it, then check female Human and character selection. Individual helmet shape/clip
coverage remains a live check; the offline probes prove socket alignment, not every helmet family.

## Prior install: in-world Human equipment (2026-10-19)

**The user reports that the body and non-helmet armor render correctly in-world.** The install recorded
below predates the helmet correction above. Server phases 2-3 are still not implemented. The
NPC/model-scoping and five-byte player model routing from Claude remain.

- `RetailRaceGeometry` no longer composes/binds the body. Geometry-only updates can occur after
  `DestroyRenderTextures` has released every temporary item texture, so the old hook could replace an
  equipped sheet with bare skin. Body composition now runs after section preparation in the synchronous,
  asynchronous and full texture-rebuild paths.
- `Engine/Source/Retail/Equipment.h` collects equipment by persistent display IDs/masks rather than
  temporary handle availability, using the stock compositor's layer limits and robe exclusions.
  `BindBody` reloads unchanged sections when needed during a partial update; failed decodes are logged
  and do not cache an incomplete sheet. Guild tabard filenames distinguish zero-display-ID emblem layers.
- `RetailRaceEquipment` runs after stock equipment geosets. It recomputes the retail foot group on every
  update (including removal/robes), checks loaded-model variant availability, and reapplies the effective
  Wrath helmet hide masks after the retail recipe. Hiding hairstyles preserves unconditional geoset zero.
  This uses existing Wrath gear; retail item/collection imports and retail helmet requirement tables are
  separate work.
- Regression cases in `RetailAssetTests`: temporary handle release retains armor, changing boots retains
  a chest whose handle was released, boot removal affects only boots, and robe removal restores trousers.
  `ctest --test-dir build/retail-satchel -C Release -R '^retail-assets$' --output-on-failure` passed.
- Original pinned Human male/female SKINs were inspected: both contain foot variants 2001-2008 and base
  groups 0/2201/3301/5101. Evidence and raw inspection inputs are outside Git under
  `G:\RetroportWork\retail-human-ingame-20261019`; see `model-geosets.json` and `inspect_geosets.py`.

Installed extended-limits Release x64 build:
`G:\3.3.5a - Dev\WowClientRetailVulkan.exe`, SHA-256
`7a6fac98552177fe0017be5890428f6d9bdbfa37ee5cf842f0af38fcb9f83d98`.
`G:\3.3.5a - Dev\LaunchRetailHumans.cmd` sets the online research config and `WOW_RETAIL_RACES=1:1`;
it forwards optional arguments such as `-d3d11`. The default remains Vulkan.
Backup executable/metadata: `G:\3.3.5a - Dev\_backups\retail-humans-20261019-052206`.
Install receipt: `G:\RetroportWork\retail-human-ingame-20261019\install-receipt.json`.
Rollback: with the client closed, restore the backed-up executable and build JSON to their original
filenames. The launcher is additive and may be removed separately.

Original live checklist: fresh launch through that launcher, log in on the equipped Human, verify chest/legs/
gloves/boots, swap boots while checking that chest/legs remain textured, remove/re-equip a helm and robe,
then relog and check both sexes plus an unchanged NPC guard. Capture `Logs\RetailRace.log` and a new
screenshot from the current installed executable if a problem remains. See the latest helmet status above.

## Goal

Bring a retail race into the 3.3.5 client 1:1: retail model, skeleton, textures and every customization option,
read straight from Blizzard's CDN (no `Esteria*.dll/.bin`, no `EsteriaAppearanceCache`), and persist the full
choice list on the server so other players see it.

## What works (verified in-game, Human male/female, creator and character select)

- Retail data read from CASC by FileDataID: DB2 tables (WDC5) incl. inline strings; layout hashes match build
  12.1.5.70448 (`88d5e8ef05ec5a8446cdec93d142d7c6`).
- Retail character M2 with **shared skeleton** (`.skel`: SKS1/SKB1/SKA1/AFID spliced into the body), legacy blend-mode
  mapping, explicit-shader degrade for skeleton models.
- Recipe resolution: choices -> geosets, texture layers, attached models (attached models are **ignored** so far).
- In-memory texture composition (body 2048x1024 sheet, hair, eyes), equipment layers pasted onto the retail sheet
  (stock 512x512 section grid maps 2x onto the retail left half), blend modes 1/5/6/7/15 etc.
- In-world players of a mapped race get the retail model (`CGUnit_C::GetModelFileName` hook) and keep the server's appearance bytes (see "In-world").
- Stock selectors (skin/face/hair/hair colour/facial hair) drive retail options by name with retail counts.
- Full options panel in the creator via the existing `EA_*` Lua contract (see below), choice names, random, preview.
- Download notice window while CDN reads block; feet/eyes/makeup fixes (see "Gotchas").

## Map of the code

Client, `Engine/Source/Retail/` (static lib `RetailAssets`, built when `WOWDECOMP_RETAIL_ASSETS=ON`):

| File | Role |
| --- | --- |
| `CascSource.*` | CascLib access. New: `ReadTable` (zero-fills encrypted blocks, `CASC_OVERCOME_ENCRYPTED`), skeleton fetch in `Read`, `BusyNotice` around fetches. |
| `RetailModel.*` | M2 preparation. New: `SpliceSkeleton`, blend table, `PrepareSkin(..., degradeShaders)`. |
| `Db2.*` | WDC5 reader (derived from WarcraftXL wxl-db2, **GPL-3.0**). Fields can be `text`. File IDs for tables are hard-coded in `TableFileDataId`. |
| `Appearance.*` | Retail customization store: `BuildForCharacter`, `OptionCount/At`, `ChoiceCount/At`, `OptionName/ChoiceName`, layout sections, `MaterialFileDataId`. Definitions are static `Field` lists. |
| `Compose.*` | BLP decode (palette/DXT1/3/5/BGRA), `BuildBlp` (raw BGRA + mips), `PaintImage`, `ComposeTextures`. |
| `BusyNotice.*` | Always-on-top "Downloading Retail data..." window on its own thread. |

Client, glue code:

| File | Role |
| --- | --- |
| `WoW/Common/ComponentCore/RetailRace.{h,cpp}` | Everything client-side per race: `RACE map` (`WOW_RETAIL_RACES="1:1,2:2"` or `Data\Retail\RetailRaces.csv`), model path, `RetailRaceGeometry` (geosets + textures), `RetailRaceBody` (body + equipment), `RetailRaceBoots`, `RetailRaceSectionCount/Arguments`, `RetailRaceCycleCommand` (`EA_*`), per-component option state `g_state`, `Logs\RetailRace.log`. |
| `CharacterComponent.cpp` | Hooks: `GeosRenderPrep` (calls `RetailRaceGeometry` after `EsteriaGeometry`; boots hook), `RenderPrepSections` and async path (`RetailRaceBody` after stock sections), `CreateBaseTexture` skips binding for retail races, `LoadBaseVariation`/`ReplaceHairTexture`/`ReplaceExtraSkinTexture` skip, `FreeComponent` -> `RetailRaceForget`. |
| `ComponentUtils.cpp` | `ComponentGetNumVariations/NumColors/ValidateSkin` return retail counts; `ComponentValidateBase/GetSectionsRecord` remap indices to row 0. |
| `EsteriaAppearance.cpp` | `EsteriaCycleCommand` first calls `RetailRaceCycleCommand`. |
| `CharacterSelection.cpp`, `CharacterCreation.cpp` | Model creation uses `RetailRaceModelPath` when mapped. |
| `Unit_C.cpp` | `GetModelFileName` returns the retail model for player units (native display, mapped race); `InitializeExtendedDisplay` skips stock validation for mapped players. |

Tools: `Tools/RetailAssets/races.cpp` (`RetailRaceProbe`: race/option/layer listing, `dump <table> [field value]`),
`compose.cpp` (`RetailComposeProbe`: renders sheets to PNG; args `<ini> <race> <sex> <prefix> [choiceId | optionId:index ...]`),
`probe.cpp` (`RetailAssetProbe`, now fetches the skeleton), `glue/patch_character_create.py` (GlueXML patch, below).

Design doc: `docs/retail-race-customization-plan.md`. Retail pipeline doc: `docs/retail-casc.md` (has a short section on this work).

## Build, run, test

Retail build tree: `build/retail-assets` (limits off, output `bin/Win/WowClientRetail.exe`); extended-limits tree
`build/retail-satchel` (output `bin/Win/WowClientRetailInventory.exe`, matches the user's daily client; a copy
staged as `G:\3.3.5a - Dev\WowClientRetailVulkan.exe`). Retail builds **default to Vulkan**; pass `-d3d11` to force D3D11. Staging convention in the dev folder: `WowClientRetail.exe` (limits-off build, D3D11 test) and `WowClientRetailVulkan.exe` (+ `.build.json`, extended-limits build from `build/retail-satchel`).

```powershell
cmake --build build/retail-assets --config Release --target WowClient RetailRaceProbe RetailComposeProbe RetailAssetProbe --parallel
# offline checks (use the research config; it has the CDN cache):
build\retail-assets\Release\RetailRaceProbe.exe  "$PWD\build\retail-research\RetailCasc.ini" 1 0        # human male options
build\retail-assets\Release\RetailComposeProbe.exe "$PWD\build\retail-research\RetailCasc.ini" 1 1 out 516:3   # PNG sheets
# in-game (must use an ONLINE config; the dev client's Data\Retail\RetailCasc.ini is mode=files and cannot serve races):
cd "G:\3.3.5a - Dev"
$env:WOW_RETAIL_CONFIG = "R:\Users\Zach\Documents\GitHub\wowdecomp\build\retail-research\RetailCasc.ini"
$env:WOW_RETAIL_RACES  = "1:1"
.\WowClientRetail.exe -d3d11
```

Logs: `G:\3.3.5a - Dev\Logs\RetailRace.log` (look timing, body binds), `Logs\RetailAssets.log`. Testing needs a human: the client is a GUI;
verify with screenshots. Offline PNG renders of composed sheets are the fast loop for texture bugs.

GlueXML: the creator Lua lives inside `Data\patch-Z.MPQ` and `Data\enUS\patch-enUS-Z.MPQ` (enUS wins; the two differ slightly,
and `EsteriaWoW\RaceGuide\sources\client-active` is **stale**). `Tools/RetailAssets/glue/patch_character_create.py` patches both
idempotently (dry run default, `--apply` backs up to `_backups\retail-glue-<time>` and verifies). It refuses while the game
runs. It uses the StormLib wrapper in `EsteriaWoW\tools\cars_mount_pack.py`.

## The `EA_*` contract (creator <-> native)

`CycleCharCustomization("EA_GET", i)` -> `value, count, label` (i = 1..29, rows with `count > 1` show); `EA_CYCLE`(i,+-1),
`EA_SET`(i,v), `EA_CHOICES`(i) -> `"0,1,2,"`, `EA_CHOICE_NAME`(i,j) (new), `EA_RANDOM`, `EA_PREVIEW_SAVE/RESTORE/END`,
`EA_RETAIL` (new; 1 if the creator's character is a mapped race). Lua patch: `EA_IS_RETAIL()` joins `EA_ACTIVE()`, labels and choice names
come from native, randomize calls `EA_RANDOM`.

## Gotchas learned (do not relearn)

- **Stock hides geosets only up to id 2000.** Retail groups above (feet 20xx, torso 22xx, eyes 33xx, customization 32xx/34xx, 51xx) all
  draw unless hidden; `RetailRaceGeometry` hides 2001..9999 then shows defaults 2001/2201/3301/5101 when the recipe does not choose that group.
- Retail group meanings: 0 hair, 1-3 facial hair, 4 gloves, 5 shin, 20 foot, 33 eyeball, 34 eyebrows; boots = 5xx + 20xx together.
- Makeup layers use blend 6/7 with **colour under alpha 0**: modulate by source alpha (Mod/Mod2x lerp, ModAdd `dst*(1+src*a)`).
- `ReplaceTexture(type, tex)` queues until the model is loaded; the stock `CreateBaseTexture` re-binds type 1 after `GeosRenderPrep`, so the body texture
  is bound again in `RenderPrepSections` and the async completion path.
- World-built models: any retail hook must test the **model**, not just the race; `ComponentData` for world units arrives with a model the client already created from stock display data.
- Dev client MPQs and the exe are locked while the game runs; check `tasklist` before staging/patching.
- Python/PowerShell heredocs here eat backslashes and `\n` in C++ string literals: write files with the editor tool or build backslashes with `chr(92)`.
- `creaturedisplayinfo.db2` has sections encrypted for unreleased content; reading must zero-fill (`ReadTable`) and the WDC5 reader skips zeroed encrypted sections.
- Name-only CASC lookup (`retail/dbfilesclient/x.db2`) fails; tables are fetched by FileDataID (community listfile).
- Option/choice display names are inline strings: offset is relative to the field's own position in the file.
- `Choices()` uses `WOW_RETAIL_CHOICES="id,id,..."` to override everything for debugging.

## In-world (fixed after the first world test; verify next)

Symptom: the player looked wrong in the world (stacked geosets, swollen feet, garbled skin) while NPC guards looked right.
Cause: world units build their model from `CreatureDisplayInfo` -> `CreatureModelData` (the stock Wrath human), not from the glue
paths, but the retail hooks keyed only on the race id, so retail geoset ids and the 2048x1024 sheet were applied to the Wrath model.
Fix, three parts:

1. **Retail logic is model-scoped.** `RetailRaceActive(component)` = race mapped **and** the component's model was created from
   `retail/<id>.m2` (`IsRetailModel` checks `m_shared->m_fileName`). `RetailRaceGeometry/Body/Boots`, the `CreateBaseTexture`
   skip, `LoadBaseVariation`/`ReplaceHairTexture`/`ReplaceExtraSkinTexture` skips and `EA_RETAIL` all use it. NPCs and non-retail
   models of the same race are untouched.
2. **Players get the retail model in the world.** `CGUnit_C::GetModelFileName` returns `retail/<id>.m2` for player units of a mapped race
   whose current display is their native display (so shapeshift/morph keep stock models). `InitializeExtendedDisplay` keeps the server's
   five bytes unvalidated for mapped players (retail counts exceed CharSections ranges).
3. **Glue-only count/remap hooks.** `RetailRaceSectionCount/Arguments` return "not mapped" when an active player exists
   (`ClntObjMgrGetActivePlayer`), so stock validation of NPC/other appearances is unchanged in the world.

Still open in-world: only the five stock bytes exist there, so other options show their first choice (phase 3 fixes this); corpse,
dress-up and inspect models still use the stock path (the model scoping keeps them from being corrupted, but they will not look retail);
other players' looks only reflect their five bytes.

## Known gaps (client)

1. Choice requirements (`ChrCustomizationReq`, `VisReq`, class/race unlocks) are not applied; every choice is offered.
2. Recipe `attached` models (skinned model elements) are ignored; bone sets, cond models, voices, particle colours ignored.
3. Equipment: helm/shoulder/tabard/cape/belt geoset logic reuses Wrath item display groups and is unchecked on retail models; hair-under-helm hides missing.
4. Option state beyond the five stock bytes is **not persisted** (resets on leaving creation, on relog, in the world).
5. Only the races you map are retail; Esteria's own races still use the old DLL/.bin path. Mapping is by Wrath race id -> retail ChrRaces id.
6. First CDN load blocks the game thread (notice window only); no async prefetch, no disk budget.
7. NPC/creature retail displays and mounts of races are separate jobs. Vulkan path untested for the composed textures.

## Next work (ordered)

**Phase 2: server (EsteriaWoW)** - see the plan doc. Summary: export retail model/option/choice tables to SQL (`RetailRaceProbe dump` is the
extraction route); table `character_retail_look(guid, choices)`; creation trailer `RCC1` after the existing payload in
`CharacterHandler.cpp`; roster trailer `RCE1` on `SMSG_CHAR_ENUM`; validation of choices per race/sex; keep the five stock bytes filled for
bots/tools. Add migration under `data/sql/updates/pending_db_characters/`. Existing patterns: `HRC1`/`HXE1`/`HXE2` trailers and the
`extraAppearance` column.

**Phase 3: network + in-world** - opcodes `0x523/0x524` (`NUM_MSG_TYPES` is `0x523`; pattern: `CMSG/SMSG_ESTERIA_INVENTORY` 0x521/0x522 in
`Opcodes.h/.cpp` and `Extensions/DigitalInventory` client side), look revision in the unit padding word, client per-GUID cache, query unknown GUIDs,
apply to other players and self; push on change.

**Phase 4:** requirements/visibility rules, barber shop, equipment geosets for retail models, `attached` models, more races, retire Esteria
DLL/bin per race (compare renders against the old catalogs before deleting), async CDN prefetch with a proper loading indicator.

## Housekeeping before continuing

- Nothing is committed. The working tree also contains the user's unrelated edits (DigitalInventory, Player_C, GuildBankFrame); commit only the
  files listed under "Map of the code" plus `cmake/RetailAssets.cmake`, `docs/retail-casc.md`, `docs/retail-race-*.md`, `Tools/RetailAssets/*`.
- `Tools/RetailAssets/glue/staged/` is generated output; do not commit it.
- Licence: `Db2.*` and `Appearance.*` derive from GPL-3.0 wxl-db2 (headers say so). Resolve distribution terms before shipping the client.
- Server rebuilds go through Docker; client Lua deployment needs the game closed and backups.
