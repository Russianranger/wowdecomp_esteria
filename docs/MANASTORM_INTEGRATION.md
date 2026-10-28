# Manastorm integration: speedflying, transmog, loot pet

Status: **Phase 0 and Phase 1 implemented in `EsteriaWoW` (uncommitted, not yet run in game).** Drafted 2026-10-27.

Progress (2026-10-27):
- 0.1 done: module, both confs and the CI clone line removed. `RequestAnticheatReport.cpp` only builds a JSON
  payload and the subscription in `ChatTransmitter.cpp` is behind `#ifdef sAnticheatMgr`, so nothing to stub.
  The `Anticheat*` hooks in `ScriptMgr` are stock AzerothCore and stay.
- 0.2 answered: the running container has `AC_UPDATES_ENABLE_DATABASES=0` (set in `apps/docker/Dockerfile`), so
  every SQL file below is imported by hand.
- 1.1/1.2 done: module copied (without its `.git` and stale review file), conf in `env/dist/etc/modules/`. The hook is
  `UnitScript::OnUnitDeath`, not `OnPlayerCreatureKill`: the latter skips hunter/warlock pet killing blows, and the
  pet-kill hook fires before the lootable flag. `OnUnitDeath` runs after both; the player is the loot recipient.
- 1.3: all four review findings were already fixed upstream (adopt at the petless branch, grace re-anchored per kill,
  `QueueWithoutPetMs` config, looted-head check). Nothing to change.
- Phase 1 deployed as `esteria-lootpet:20261027` and confirmed in game (solo kill with a companion out).
- 2.1 done, deployed as `esteria-transmog:20261027`. Folder must be `mod-transmog` (loader `Addmod_transmogScripts`);
  conf is `transmog.conf`. A different transmog module was removed in `a21c46b5d` (2026-09-22) and left its tables:
  `custom_unlocked_appearances` (4,326 rows) has the same schema and is kept. Imported by hand: both characters
  files and all five world files; the auth `acore_cms_subscriptions` table already existed. The window's NPC is
  **190010** (`TMOG_VENDOR_CREATURE_ID`), not 190012. `AddonChannel = 1` is already set.
- 2.2: the addon goes into the client's loose `Interface\AddOns\Manastorm_Transmog` like any addon, not into the
  exe (41 MB of BLPs) and not via Manastorm's `SFile2-Core` patch-Z change. A copy is in `Addons/Manastorm_Transmog`.
  The addon calls the 2.3 engine methods unguarded, so it needs the new exe.
- 2.3: Vulkan `DeviceReadTexture` reuses the screenshot read-back (`frame::ReadImage`, generalised from `ReadScene`).
  D3D9/DX11 return false (base default), so cards stay blank there. Ported: `ModelSnapshot`, the `Snapshot:` texture
  branch in `CSimpleTexture::SetTexture`, the x64 sort-order fix in `CRenderBatch`, `SetTilt`/`GetModelHeight`/offscreen
  render in `CSimpleModel`. Skipped: Manastorm's per-draw D3D tracing (`TraceQueue`/`TraceDraw`/`TraceState` call
  sites). The plan missed `CharacterComponent.cpp/.h` (silhouette): 3-way merged against Manastorm `d507eb2b^`, renamed to
  our identifiers (`VARIATION_*`, `void*` textures), with `EsteriaGeometry`/`RetailRaceGeometry` kept. DressUpModelFrame
  merged the same way. Client built clean and installed to `G:\3.3.5a - Dev\Wow.exe` (backup
  `_backups\transmog-20261027-234138`).
- 2.4: nothing to remove. No local wardrobe/morph was ever built in this tree: `Morph.h` is in the external Eclipse
  source, and `ESTERIA_NATIVE_FEATURE_INTEGRATION.md` only proposed importing it (now marked dropped). EsteriaInventory's
  only hit is stock ctrl-click `DressUpItemLink`.
- Ethereal white boxes (HD `patch-C.mpq` skin2/skin3 lost under later full `CreatureDisplayInfo.dbc` copies): fixed with
  `EsteriaWoW/tools/hd_creature_skins.py`, confirmed in game.
- 3.1 server: the core edits of `b915c2b1b` reverse-applied (opcodes 0x527/0x528, handler, `shared/Skyriding`;
  `NUM_MSG_TYPES` back to 0x527); module and confs removed. 3.1 client: `Extensions/Skyriding`, `Engine/Source/Skyriding`,
  `Tools/SkyridingTests`, `Addons/EsteriaSkyriding` and their registrations removed; `OnUnitPlotLocalMove` and
  `OnUnitMountSequence` removed (no other user), stock plot restored in `Movement.cpp`; skyriding docs in `docs/archive/`.
  `RetailModel.cpp`'s two-texture ribbon path is generic and stays.
- 3.2: module, conf and both SQL files in. `Talents.MaxPoints` is 103 in code and conf (the README's 13/140 is stale).
- 3.3: new files copied (`SpeedFlying`, `TalentUI`, addon); `SpeedPanel.h` skipped (no users). Manastorm logs through its
  `ModernAssets` module, which we lack: `SpeedFlying.cpp` writes `Logs/speedflying.log` itself. The engine edits were
  merged per file, per commit (`231fc33e fd558774 5ed62256 ed906c6f 2dde87c6 c4ba3795 18842a2f aa6b02e6`) onto our copies:
  `Unit_C.cpp/.h` (chooser, rider-driven mount anim, tier-walk hang fix; our Two Forms self-mount/Dracthyr branches kept
  ahead of the new flying branch), `Player_C.cpp`, `Camera.cpp`, `M2Model.cpp` (506 animation-id ceiling lifted; our
  Centaur pickup remap kept), `Input.h/.cpp` (`OsInputIsAppActive`). Skipped: the temporary sequence CSV dump, the
  `M2Modern` remap (no ModernAssets importer here), `ConsoleDevice.cpp` D3D9Ex opt-in.
- Phase 3 deployed: server `esteria-speedflying:20261028` (starts clean); client installed (backup
  `_backups\speedflying-20261028-003033`) with `Interface\AddOns\Manastorm_Speedflying`. Not yet tested in game (3.4).
- Boost clip follows Retail AnimationData's fallback (1706 -> 1638 -> 135): the Mastercraft Gravewing has no 1706,
  and the live CDN copy (build 70575) is byte-identical to ours.

## Esteria additions: skyriding abilities on speedflying (2026-10-28)

Carried over from the old skyriding (tag `skyriding-final`): four action-bar spells and the visuals.

| Spell | ID | Cost | Effect |
|---|---|---|---|
| Lift Off | 300100 | free, 3 s cooldown | on the ground on a flying mount: sets flight, holds ascend 1.1 s |
| Skyward Ascent | 300101 | 1 boost stack | holds ascend 1.2 s, AdvFlyFlapUp clip |
| Whirling Surge | 300102 | 1 boost stack | speedflying's server boost, a barrel roll, the Surge impact effect |
| Flap | 300103 | free, 1 s cooldown | holds ascend 0.3 s, AdvFlyFlap clip |

- Server: `mod-mana-speedflying` `spell_speedflying_ability` checks mounted / can fly / airborne / stacks (under
  `g_lock`), spends the stack, re-announces the stacks and whispers `WXLMS\tA:<L|U|W|F>`. Every character learns the
  four spells at login. Rows: `data/sql/db-world/20261028_speedflying_abilities.sql` (imported by hand).
- Client spell rows: `Tools/RetailAssets/speedflying_abilities.py` writes them (and the retail icon aliases
  4640492/4640498/4640477/4640490) into the active supplemental registry. **`native_import.py` rebuilds that registry
  on every pack install, so run the script again after one.** It also generates the server SQL from the same rows.
- Client behaviour: `Listener.lua` queues `A:` letters in `Manastorm_AbilityQueue`; `SpeedFlying.cpp` drains it. The
  lifts use the stock ascend (normal flight packets), not a movement-plot push: a hovering player sends no movement
  steps, so a push would not move them. Mount clips ride the existing boost request (`CGUnit_C::m_flgtOneShot` picks
  the ladder). Wind tunnel (kit 159809, six FX_Speed4 models) shows while diving or at 90% of the dive speed; turns lean
  the mount; all local-player only.
- Deployed: server `esteria-skyabilities:20261028`, client backup `_backups\skyabilities-20261028-100804`, registry
  backup `_backups\registry-20261028-100014`. Not yet tested in game.
- Retail Skyriding HUD: `Addons/Manastorm_Speedflying/Skyriding.lua` (from the old `EsteriaSkyriding` HUD, retail art
  via `retail\<FileDataID>.blp`). Vigor pips show the boost stacks and Hud.lua's recharge clock, replacing Manastorm's
  orb row (its Talents button stays). Buttons: Surge Forward (the boost; sets `Manastorm_SurgeRequest`, which
  `SpeedFlying.cpp` spends like a SPACE press), Skyward Ascent / Lift Off (secure macro on `[flying]`), Whirling
  Surge, Flap. Action keys 1-4 press them on a skyriding mount (out of combat changes only). Shown by a `[mounted]`
  state driver and faded out off AdvFly mounts, since secure frames cannot be hidden in combat. Flap's icon is
  now 4640486 (gliding) so it differs from Surge Forward. Client backup `_backups\skyhud-20261028-102304`.

Goal: replace Esteria Skyriding with Manastorm's speedflying, add Manastorm's transmog window, and add the
server-only loot pet, with `mod-anticheat` removed from the server.

## Sources

| Piece | Path | Notes |
|---|---|---|
| Client reference | `GitHub\manastorm` | A fork of the same wowdecomp upstream. GPL-3, WarcraftXL copyright. |
| Speedflying server | `GitHub\mod-mana-speedflying` | ~2.2k lines C++, 2 SQL files, conf. |
| Transmog server | `GitHub\mod-transmog` | "transmog-plus-ui", ~2.2k lines C++, SQL, conf, own addon folder. |
| Loot pet server | `GitHub\mod-mana-loot-pet` | ~1.1k lines C++, no SQL, no client. |
| Our server | `GitHub\EsteriaWoW` (AzerothCore, branch `main`) | Modules in `modules/`, image built by Docker. |
| Our client | `GitHub\wowdecomp` (branch `Esteria`) | Daily client: see `deploy-to-dev-client` memory. |

## Decisions already made

- `mod-anticheat` is **removed**, not patched. This also drops the need for any speed exemption for 600-1600% flight.
- Local wardrobe/morph is removed; flight animation is Manastorm's.
- Skyriding is replaced, not kept alongside. The two movement models cannot coexist.
- Transmog UI is Manastorm's `Manastorm_Transmog`. The `addon/` folder shipped inside `mod-transmog` is not used.

## Decisions (resolved 2026-10-27)

1. **Local wardrobe/morph: dropped.** Server transmog is the only appearance system. Phase 2.4.
2. **Skyriding tag: done.** `skyriding-final` created locally (not pushed) on `wowdecomp` at `ff7b6ecf` and on
   `EsteriaWoW` at `317859bea`. Both are the current HEADs, so any uncommitted skyriding edits are not in the tag.
3. **Flight animation: Manastorm's AdvFly chooser is used**, ours is deleted. Phase 3.3.
4. **Loot-pet hook:** change the module to use `OnPlayerCreatureKill` instead of patching the core. Phase 1.2.

---

## Phase 0: Server baseline and anticheat removal

All server steps are in `EsteriaWoW`. Do this first so every later test runs on the final server shape.

### 0.1 Remove `mod-anticheat`

Footprint found:

| Where | What |
|---|---|
| `modules/mod-anticheat/` | The module itself (src, conf, SQL, icon). Delete the directory. |
| `env/dist/etc/modules/Anticheat.conf`, `Anticheat.conf.dist` | Config. Delete. |
| `modules/mod-esteria-skyriding/src/mod_esteria_skyriding.cpp` | Sets the `EsteriaSkyriding` CustomData flag the anticheat read. Goes away with Phase 3. |
| `modules/mod-chat-transmitter/src/ChatTransmitter.cpp:13` | `#if __has_include("mod-anticheat/src/AnticheatMgr.h")`: guarded, so it compiles without the module. |
| `modules/mod-chat-transmitter/src/Requests/RequestAnticheatReport.cpp` | An anticheat report request. **Check:** it must not call into the module unguarded. If it does, stub or delete it. |
| `apps/ci/ci-install-modules.sh:9` | Clones `mod-anticheat` for CI. Remove the line. |
| Database | `charactersdb_anticheat.sql` creates tables in `acore_characters`; `Acore_strings.sql` adds strings in world. **Leave the tables in place.** Dropping them is optional cleanup and is not reversible. |
| `build/` | Stale CMake cache and generated `ModulesLoader.cpp` mention it. A fresh configure removes them. Do not hand-edit. |

Steps:
1. `git rm -r modules/mod-anticheat` plus the two conf files and the CI line.
2. Build the worldserver. Fix any unguarded reference in `mod-chat-transmitter`.
3. Verify at runtime: no "Anticheat" lines in `Server.log`, and `.anticheat` GM commands no longer resolve.

Risk: players lose all movement validation. The speedflying module sets speed only for flying players, and a
forged link can at worst pick one of the configured state speeds, but nothing now stops speed hacks in general.
Accepted by decision.

### 0.2 Database updates

`worldserver.conf` has `Updates.EnableDatabases = 7` in `env/dist`, but the skyriding README says the Esteria image
starts with database updates off. Check what the running container actually uses. If updates are off, every SQL
file in this plan is imported by hand.

---

## Phase 1: Loot pet (smallest, server-only)

### 1.1 Install

1. Copy `mod-mana-loot-pet` into `EsteriaWoW/modules/`.
2. Copy `conf/mod-mana-loot-pet.conf.dist` to `env/dist/etc/modules/` as `mod-mana-loot-pet.conf`.
3. No SQL.

### 1.2 Required fix: the kill hook

The module overrides `OnPlayerCreatureKillCredit`. **Our core has no such hook**; it has `OnPlayerCreatureKill`
(`PlayerScript.h:259`, fired at `Unit.cpp:14333`). The lootable flag is set earlier in the same function
(`Unit.cpp:14242`), so `OnPlayerCreatureKill` fires after the corpse is lootable, which is what the module needs.

Change the override in `mod_mana_loot_pet.cpp:638` to `OnPlayerCreatureKill`. The module's script registration must
also use the matching `PLAYERHOOK_ON_CREATURE_KILL` enable list entry, if it declares one. A core patch is not needed.

### 1.3 Known bugs to fix before shipping

`review_git_diff_df087996.md` in the module lists findings. Treat these as work items and re-verify each against the
current source:

- A companion summoned after the kill is never adopted into a petless queue. The queue is deleted at the end of the
  grace period instead of walked.
- The 60 s grace is measured from the wrong start when a corpse joins an existing waiting queue.
- The grace is hard-coded and not a config key (`QueueWithoutPetMs` exists in the README, so check whether this was
  already fixed).
- A petless queue is never re-validated, so it can hold the global fetch counter above zero for the whole grace.

### 1.4 Interactions to test

| Module | Concern |
|---|---|
| `mod-aoe-loot` | May loot the corpse before the pet arrives, or loot corpses the pet queued. |
| `mod-loot-beam` | Beam should clear when the pet settles the corpse (lootable flag cleared). |
| `mod-loyal-steed`, `mod-starting-pet` | Both deal with pets; confirm they use the minipet slot the module expects. |
| `mod-individual-progression`, `mod-random-enchants`, `mod-reward-*` | Hook the same loot events; the module re-raises money hooks only, not item hooks (documented gap). |

### 1.5 Acceptance

Solo-kill a mob with a companion out: the pet walks to the corpse, money and items arrive, a full bag mails the
overflow. A grouped kill is untouched. No client change was needed.

---

## Phase 2: Transmog

### 2.1 Server

1. Copy `mod-transmog` into `modules/` (folder name `mod-transmog-plus` internally).
2. Import SQL into `acore_characters` and `acore_world` (module `data/`).
3. Copy the conf to `env/dist/etc/modules/mod_transmog_plus.conf` and set prices and restrictions.
4. Rebuild. After start, spawn the transmog NPC as a GM: `.npc add 190012`, then make it permanent in the world DB.
5. Do not copy the module's `addon/` folder.

Protocol (shared with the client addon): addon whispers to self. Client sends `TMOGC` and server answers `TMOGS`.
Requires `CONFIG_ADDON_CHANNEL` on. Check it is enabled in `worldserver.conf`.

Test with the gossip menu first (works without the addon) to prove the server before touching the client.

### 2.2 Client: addon and textures

1. Copy `manastorm/Addons/Manastorm_Transmog/` (Lua, `.toc`, `Textures/*.blp`) into `wowdecomp/Addons/`.
2. Loading: Manastorm loads it from `patch-Z.mpq` via a change in `Engine/Source/BlizzardCore/SFile2-Core.cpp` and
   `Tools/deploy-ui.ps1`. Compare against how our `Addons/EsteriaInventory` is loaded (built in via
   `BuiltinAddOns.cpp`, or MPQ) and use whichever we already do. Do not carry both mechanisms.
3. Texture paths in the Lua are `Interface\AddOns\Manastorm_Transmog\Textures\...`. The speedflying talent panel in
   Phase 3 reuses these textures, so install this addon before Phase 3.

### 2.3 Client: engine

New files copy cleanly:
- `Engine/Source/Frame/ModelSnapshot.h/.cpp`

Files that exist on both sides and must be merged by hand (do not overwrite):

| File | Manastorm change | Size of divergence from ours |
|---|---|---|
| `WoW/Source/Ui/DressUpModelFrame.cpp/.h/_Scripts.cpp` | Undress/TryOn sheathed, body hidden, silhouette, human preview, snapshot Lua API | ~210 / 20 / 140 differing lines |
| `Engine/Source/Frame/CSimpleModel.cpp/.h/_Scripts.cpp` | `SetTilt`, offscreen render, scale by model height | ~70 differing lines |
| `Engine/Source/Frame/CSimpleRender.cpp` | Snapshot render path | ~36 lines |
| `Engine/Source/Frame/CRenderBatch.cpp` | Snapshot texture handling | small |
| `Engine/Source/Gx/CGxDevice/CGxDevice.h` | New virtual readback and render-target entry points | small |
| `Engine/Source/Gx/CGxDeviceD3d`, `CGxDeviceD3d9Ex` | Manastorm's readback implementation | reference only |
| `WoW/Source/DB/DBClient/DBCache.cpp` | One-line change | trivial |

**Main risk: graphics backend.** Manastorm implements snapshot readback only for D3D9 and D3D9Ex. Our client's
renderer is **Vulkan** (see the `deploy-to-dev-client` memory), and `DX11WXL.md` describes a DX11 path. The
`CGxDevice` readback entry points must be implemented for every backend we ship, or item cards render blank.

Order within 2.3:
1. Add `ModelSnapshot` and the `CGxDevice` interface.
2. Implement Vulkan readback (create ARGB render target, render, copy to staging buffer, map). Add DX11 if that backend is shipped.
3. Merge `CSimpleModel`, `CSimpleRender`, `CRenderBatch`.
4. Merge `DressUpModelFrame`.
5. Add the CMake entries for the new source file.

### 2.4 Existing wardrobe / morph: remove

Decision: drop it. Before deleting, find every user:
- `Morph.h` and whatever implements it (grep `Morph`, `Wardrobe`, `FakeItem`).
- The dress-up and transmog references in `Addons/EsteriaInventory/EsteriaInventory.lua`.
- `ESTERIA_NATIVE_FEATURE_INTEGRATION.md` sections describing it, and any Lua API or CVar it registers.
Remove the code, then update that doc. The engine appearance code in `Engine/Source/Retail/Appearance.cpp` is for
races and creation, not wardrobe: leave it. Server-applied appearances reach the client through ordinary visible-item
fields, so nothing local is needed.

### 2.5 Acceptance

Open the window at the NPC, preview an owned appearance, apply it, relog (it persists), and a second client sees
the transmogrified look. Item cards show the right model on Vulkan. Custom Esteria races preview on the human
silhouette without a crash.

---

## Phase 3: Speedflying replaces Skyriding

Prerequisite: Phase 0 done (anticheat gone) and Phase 2 addon installed (the talent panel reuses its art).

### 3.1 Tag, then remove skyriding

Both repos are already tagged `skyriding-final` (local only). Retail HUD art research in `SKYRIDING_RETAIL_ASSETS.md` stays.

Client (wowdecomp):

| Remove | Notes |
|---|---|
| `Extensions/Skyriding/`, `Engine/Source/Skyriding/`, `Tools/SkyridingTests/` | The extension, shared headers, tests. |
| `Addons/EsteriaSkyriding/` | HUD. |
| Entry in `WoW/Source/UIUtil/BuiltinAddOns.cpp` and `WoW/Source/UIUtil/CMakeLists.txt` | Built-in addon registration. |
| CMake option `WOWDECOMP_EXTENSION_SKYRIDING` | Plus any `add_subdirectory`. |
| Call to `OnUnitPlotLocalMove` in `WoW/Source/Object/Movement.cpp` (~line 5226) | Revert to native plot. The hook declaration in `UnitScript.h/.cpp` and `ExtensionMgr.h` may stay if no other extension uses it. |
| `Engine/Source/Retail/RetailModel.cpp` skyriding speed-aura shader paths | Keep only if still used by a mount effect. Otherwise remove. |
| `docs/SKYRIDING_*.md`, `Esteria_Skyriding_Implementation_Guide.*` | Move to `docs/archive/` rather than delete. |

The AdvFly animation state machine in `Extensions/Skyriding` (via `OnUnitMountSequence`) is deleted with it, since
Manastorm's chooser replaces it (3.3). Remove the `OnUnitMountSequence` call at `Unit_C.cpp:4705` and its declaration in
`UnitScript.h/.cpp` once the new chooser is in, so there is never a moment with two animation selectors.

Server (EsteriaWoW):

| Remove | Notes |
|---|---|
| `modules/mod-esteria-skyriding/` | Module. |
| `Opcodes.h/.cpp`: `CMSG_ESTERIA_SKYRIDE` 0x527, `SMSG_ESTERIA_SKYRIDE` 0x528 | Restore `NUM_MSG_TYPES` to 0x527. |
| `MovementHandler.cpp`: `HandleEsteriaSkyrideOpcode` | The drain handler. |
| `src/server/shared/Skyriding/` | Shared header copies. |
| `docker-compose.twoforms.yml` image tag `esteria-skyriding:...` | Point at the new image. |
| World DB table `esteria_skyriding_mounts` | Leave in place; drop later if desired. |

Note: `git status` already shows `docker-compose.twoforms.yml` modified. Commit or stash that separately first.

### 3.2 Server install

1. Copy `mod-mana-speedflying` into `modules/`.
2. Apply both SQL files to `acore_characters`: `20260101_speedflying_talents.sql`, `20260102_speedflying_talents_rework.sql`.
3. Copy the conf to `env/dist/etc/modules/mod-mana-speedflying.conf`. Reconcile `Talents.MaxPoints`: the README gives
   both 13 and 140. Read the code default in `mod_mana_speedflying.cpp` and set it deliberately. Raise
   `Talents.MaxPointsPerMinute` for a large tree.
4. Confirm `CONFIG_ADDON_CHANNEL` is on (shared with Phase 2).
5. Check interaction with `mod-fly-anywhere` and `mod-account-mounts`: a mount-speed aura can briefly fight the
   module's blend. Test with an account-wide mount.

With `mod-anticheat` gone no exemption is needed. If it is ever re-added, an exemption will be required.

### 3.3 Client port

Copy cleanly (new files):
- `WoW/Source/Ui/SpeedFlying.cpp/.h`
- `WoW/Source/Ui/TalentUI.cpp/.h` (the circular tree; our `TalentInfo.*` is the stock talent frame and is separate)
- `Addons/Manastorm_Speedflying/` (Hud, Link, Listener, Talents Lua + `.toc`)
- `WoW/Source/Ui/SpeedPanel.h`: its own header calls it stale, so confirm whether anything uses it before copying.

Port by hand (our files diverge widely from Manastorm's; do not copy whole files):

| File | Change |
|---|---|
| `WoW/Source/Object/ObjectClient/Player_C.cpp` | Call `SpeedFlying::Update(time)` from the active player's `PreAnimate`. |
| `WoW/Source/UIUtil/Camera.cpp` | `FOV()` adds `SpeedFlying::CameraFovBoost()`; add the dive-shake apply/restore pair (`ApplyDiveShake`, `RotateAbout`, and the restore in the update callback). |
| `WoW/Source/Object/ObjectClient/Unit_C.cpp` and `Unit_C.h` | Port Manastorm's AdvFly flight animation (see below). |
| `WoW/Source/Ui/CMakeLists` / source lists | Add the new `.cpp` files. |
| `WoW/Source/ConsoleDevice/ConsoleDevice.cpp` | Skip: that D3D9Ex opt-in change is unrelated to speedflying. |

AdvFly detection: the client marks a mount AdvFly by testing its model for animation ids
`1532, 1722, 1724, 1524, 1520, 1522, 1530, 1704, 1678`. Verify one retail drake from our import pipeline reports
`advfly=1` in the log. Stock mounts and druid forms must report 0.

**Animation: port Manastorm's AdvFly chooser.** Our tree has no `ChooseMountFlightAnim`; the mount clip is chosen
through `sExtensionMgr->OnUnitMountSequence` (`Unit_C.cpp:4705`). In Manastorm the pieces are in `Unit_C.cpp` and
`Unit_C.h`:

| Piece | Manastorm location |
|---|---|
| `CGUnit_C::ChooseMountFlightAnim(const M2Data*, uint32_t riderAnim)` and its state holds | `Unit_C.cpp` ~4768 onward, declared `Unit_C.h:906` |
| Caller that applies the chosen clip every frame (state upkeep is deliberately outside the chooser) | `Unit_C.cpp` ~1467-1530 |
| Rider-translation/swim handling and the "only re-push when the id or speed differs" logic | `Unit_C.cpp` ~5179-5362 |
| Climb/dive thresholds replaced by `SpeedFlying::ClimbSin()` / `DiveSin()` | `Unit_C.cpp` ~4506 |
| New members (boost-clip re-push flag and state timers) | `Unit_C.h` ~1065 and nearby |

Steps: copy the chooser and its helper members, hook the two call sites into our mount animation update in place of
the `OnUnitMountSequence` call, then delete the extension hook. Our `Unit_C.cpp` differs from Manastorm's by ~2400 lines,
so port by function, not by file, and build after each piece. The chooser reads the same `M2Data` AdvFly ids the
AdvFly test uses, so retail drakes from our import pipeline should work unchanged. Check a mount with no AdvFly clips
falls back to the stock flight animation.

### 3.4 Acceptance

Mount a retail drake: HUD frame appears, boost stacks show, dive reaches the configured speed, FOV widens and the
camera shakes, double-tap Space boosts. Open the talent tree, spend a point, confirm and relog. Stock mount and druid
flight form fly at normal speed with no HUD. Server rejects nothing when flying at 600%+.

---

## Phase 4: Build, deploy, verify

1. Server: rebuild the worldserver image once with all three modules and the removals. Tag the previous image first
   (`esteria-skyriding:20261025d` is the current one). Update `docker-compose.twoforms.yml` to the new tag.
2. Client: build as in `AGENTS.md` (`Tools/build.ps1`, x64). For the daily client use
   `cmake --build build/retail-satchel --config Release --target WowClient` and install per the `deploy-to-dev-client`
   memory: back up `G:\3.3.5a - Dev\Wow.exe` into `_backups\<name>-<time>` first, close any running game, then copy.
3. UI changes under `Data/custom.MPQ/Interface/`: run `Tools/deploy-ui.ps1` with the client closed.
4. Logs after a test: `Logs\RetailRace.log`, `Logs\vulkan.log`, `Logs\snapshot.log`, `WowClient_crash.log`, and the
   server `Server.log` / `Errors.log`.

## Rollback

| Piece | How |
|---|---|
| Anticheat | `git revert` the removal commit; tables were never dropped. |
| Loot pet | Delete the module folder, or `ManaLootPet.Enable = 0`. |
| Transmog | Delete the module folder; stored rows stay harmless. Client falls back to no window. |
| Speedflying | `ManaSpeedFlying.Enable = 0` and `Talents.Enable = 0`, or remove the module. Restore skyriding from the `skyriding-final` tag (local; push it if you want an off-machine copy) and the previous image/exe backups. |

## Risks (ranked)

1. **Vulkan (and DX11) snapshot readback** is net-new engine work with no reference implementation. Largest unknown.
2. **Loot-pet hook mismatch** is a small fix, but the module's own review lists unfixed critical bugs.
3. **No anticheat** by decision: all speed and teleport validation is gone.
4. **Hand-merging** `DressUpModelFrame`, `CSimpleModel`, `Camera.cpp` and `Unit_C.cpp` against large divergence.
5. **Licensing:** Manastorm is GPL-3. License terms of `mod-transmog`, `mod-mana-speedflying` and
   `mod-mana-loot-pet` were not checked.
6. **Skyriding work is lost** from the live build. Mitigated by the tag, but it was untested in game anyway.

## Not verified when this was written

Nothing here has been built or run. Module sources were read through READMEs and targeted greps only, not line by
line. `RequestAnticheatReport.cpp` was not opened. The `OnPlayerCreatureKill` firing order was read from two line
numbers, not traced.
