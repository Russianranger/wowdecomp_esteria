# Skyriding handoff (2026-10-25)

Pick-up document for the next session. Read this first, then `SKYRIDING_CHECKPOINT.md` (status log) and
`SKYRIDING_RETAIL_ASSETS.md` (every retail FileDataID). Design: `SKYRIDING_ARCHITECTURE.md`, `SKYRIDING_PHYSICS.md`,
`SKYRIDING_PROTOCOL.md`; original brief: `Esteria_Skyriding_Implementation_Guide.md`.

**Next goal:** reproduce retail's on-screen wind/speed effects and give mounts retail skyriding animations (retail
drake models with their `AdvFly*` animation sets, plus a fallback for WotLK mounts).

## Where things stand

Working in game, confirmed by the user: skyriding activates on a flying mount, flies with momentum, and the
abilities work. The retail-art HUD is installed but **not yet seen in game**.

| Piece | State |
|---|---|
| Physics, resources, wire, envelope (shared headers) | done; `Tools/SkyridingTests` passes 15 groups |
| Client controller + Lua API + HUD | deployed |
| Server module, opcodes, anticheat exemption | deployed |
| Flying everywhere (no-fly zones opened, open world only) | deployed, both sides |
| Retail HUD art | deployed, untested in game |
| Wind/speed effects | mapped only |
| Retail mount models / AdvFly animations | mapped only |
| Commits | **nothing committed in either repo** |

### Deployed right now

- Client: `G:\3.3.5a - Dev\Wow.exe` = `wowdecomp/bin/Win/WowClientRetailInventory.exe`, built from
  `build/retail-satchel`. Original pre-skyriding exe: `G:\3.3.5a - Dev\_backups\skyriding-20261025-030229\Wow.exe`.
- Server: `EsteriaWoW/docker-compose.twoforms.yml` -> `esteria-skyriding:20261025d` (image `57b2d0eb91ea`).
  Pre-skyriding image: `esteria-twoforms:20261024g`.
- World DB: table `acore_world.esteria_skyriding_mounts` (empty = every flying mount qualifies).
- Server config: `EsteriaWoW/env/dist/etc/modules/mod_esteria_skyriding.conf` (+ `.dist`).

### Uncommitted changes

wowdecomp: `WoW/Source/Extension/ExtensionMgr.h`, `ScriptDefines/UnitScript.{h,cpp}`, `WoW/Source/Object/Movement.cpp`,
`WoW/Source/UIUtil/{BuiltinAddOns.cpp,CMakeLists.txt}`; new `Addons/EsteriaSkyriding/`, `Engine/Source/Skyriding/`,
`Extensions/Skyriding/`, `Tools/SkyridingTests/`, `docs/SKYRIDING_*.md`, the guide `.md/.pdf`.
(`DX11WXL.md`, `TWOFORMS.md` were untracked before this work.)

EsteriaWoW: `Opcodes.{h,cpp}`, `WorldSession.h`, `Handlers/MovementHandler.cpp`, `modules/mod-anticheat/src/AnticheatMgr.cpp`,
`docker-compose.twoforms.yml`; new `modules/mod-esteria-skyriding/`, `src/server/shared/Skyriding/`. Leave
`env/dist/data-backups/` (310 MB of DB dumps) and the runtime logs out of any commit.

The four `Engine/Source/Skyriding/*.h` headers must stay byte-identical with `EsteriaWoW/src/server/shared/Skyriding/`
(copy with CRLF stripped: `sed 's/\r$//'`).

## Build, deploy, verify

```bash
cmake --build build/skyriding-tests --config Release && build/skyriding-tests/Release/skyriding_tests.exe
```

```bash
cmake --build build/retail-satchel --config Release --target WowClient --parallel
```

Client install: close the game, copy `bin/Win/WowClientRetailInventory.exe` over `G:\3.3.5a - Dev\Wow.exe`.

Server image (about 5 min with ccache, about 30 min if `Opcodes.h` or a shared header changed), run from `EsteriaWoW`:

```bash
docker build -f apps/docker/Dockerfile --target worldserver --build-arg USER_ID=1000 --build-arg GROUP_ID=1000 --build-arg DOCKER_USER=acore -t esteria-skyriding:<tag> .
```

Then set the tag in `docker-compose.twoforms.yml` and run, from `EsteriaWoW`:

```bash
docker compose -f docker-compose.yml -f docker-compose.override.yml -f docker-compose.twoforms.yml up -d --no-deps ac-worldserver
```

**Always compare `docker inspect ac-worldserver --format "{{.Image}}"` with the tag's image ID.** Killing a
`docker build` does not stop BuildKit: an abandoned build once finished later, tagged first, and the server came up
on stale code. Check `docker buildx history ls` before starting a new build.

Logs: client `G:\3.3.5a - Dev\Logs\Extensions.log` (`Skyriding: hello sent`, `server answered`, `begin`),
`Logs\RetailAssets.log` (failed CDN files); server `docker logs ac-worldserver | grep Skyriding` (`hello from`,
`may / may not skyride, reason N`, `failed the ... check`, `flying opened in 65 areas`).

## Lessons from this session

- **Hello race:** the client's first hello can arrive before the server counts the player as in world, and
  `STATUS_LOGGEDIN` packets are then dropped silently. Fixed: the server answers every hello (Resources message), the
  client resends every 3 s until answered (max 10).
- **Flight in Dalaran / no-fly zones** failed before skyriding ever started. `Skyriding.FlyEverywhere = 1` (server)
  and `SkyridingClientDB` (client) set AreaTable flag 0x400 and clear 0x20000000 in memory for every non-instance area.
- **Mount eligibility** checks the player's auras (`SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED`), because many mounts
  put flight speed in a triggered spell.
- **The server calls the `const WorldPacket&` overload** of `ServerScript::CanPacketReceive`.
- **The envelope's first version let a steady climb through** (per-sample slack compounded). The shared
  `SkyridingEnvelope.h` and its tests are the reference now; change it only with the tests.
- **CDN:** the client's `Data/Retail/RetailCasc.ini` (pinned build `88d5e8ef...`) fetches uncached icons, textures and
  models fine; DB2 files fail with CascLib 6002 on both builds, so use the wago.tools CSVs instead.
- **The server image runs with `AC_UPDATES_ENABLE_DATABASES=0`:** module SQL must be applied by hand.

## Research data (kept)

`wowdecomp/build/skyriding-research/` (git-ignored, about 390 MB):
- `listfile/listfile.csv`: community listfile 202610241710 (FDID;path); `listfile/hits.csv`: skyriding matches.
- `db2/*.csv`: wago.tools exports for build 12.1.5.70575 (spell visuals, positioners, atlases, mounts, displays,
  models) and `animnames.csv` (animation ID -> name, from WMVx).
- `trace.py <spellID ...>`: prints spell -> SpellVisual -> kit -> model / screen / camera effects. Run from that folder.
- `casc/Current.ini`: CDN config pinned to 12.1.5.70575 (cache goes to `casc/Cache/`); `casc/models/`: drake models
  4227968, 4278602, 4281540, 4252337, 4252339, 5143343, 5305977, wind model 4579907, two textures.
- Fetch any file: `build/retail-satchel/Release/RetailAssetProbe.exe <config.ini> retail/<FDID>.<ext> --extract <out>`
  (run from the config's folder; `--inspect` validates the model layout).

## Next step 1: on-screen wind / speed effects

Source (all in `SKYRIDING_RETAIL_ASSETS.md`): retail visual kit 159809, played by hidden spells 379415 / 373404 /
432158 ("AdvFlyDown + Speed Aura") while diving or flying fast.
- **Models:** `spells/FX_Speed4_Aura.m2` 4579907 (scale 1.25, anim 158, end 159), `FX_Speed4_Aura01.m2` 4579908 (scale
  1.25, anim 214, end 159), and four copies of `FX_Speed4_Aura02.m2` 4640283 (positioners 1841/1842/1846/1847:
  +-20 deg yaw, -5 deg pitch, two rolled 180 deg and raised 0.16).
- **Sound and screen:** two sound kits (200185, 200186), the dive loops `Sound/Spell/DragonridingAdvFlyDown...`
  (4684310/12/14), and screen effect 1918 (a light/ambience override with no full-screen texture; probably skip).
- **Other ability effects:** Surge Forward `FX_SurgeForward_Impact.m2` 4579917 on attachment 17; Skyward Ascent /
  Lift Off model 4290528.

Plan:
1. Find how the client spawns world-attached M2s for spell visuals (start at `CGUnit_C` effect code: `CEffect`,
   `m_procEffects`, `AddEffect`, `PlaySpellVisualKit`, and the ObjectScript hook `OnObjectSpellVisualKit`). Reuse that
   instead of a new renderer path. The mount/rider attach in `CGUnit_C` (`AttachToParent(m_mountModel, ...)`) is a
   second option.
2. In `Extensions/Skyriding`, create the six models when skyriding starts. Load them as `retail\<FDID>.m2`; the
   retail source resolves their skins/textures through the model's own SFID/TXID chunks. Attach them to the rider's
   mount model root with the offsets above, and hide or destroy them on landing, End, or leaving the world.
3. Drive alpha (or start/stop) from the simulator: on while `AirState::Dive` or speed >= `thrillSpeed`, faded by speed.
   Play the kit's anim IDs (158/214 loop, 159 end).
4. Positioner source points (`SrcVal` 15/16/19, 60-63) are not decoded; verify by eye whether the effect should follow
   the rider or the camera.
5. Optional: Surge / Ascent impact effects on ability use; dive sound loop.
6. **Remote riders:** the client does not know who else is skyriding. Add a protocol message (e.g. ServerMsg 6
   `RiderState`: guid + flags, sent to observers on Begin/End), or show effects only for the local player at first.
   That message is also what remote mount animations need (step 2).

## Next step 2: mount animations and retail skyriding mounts

Facts:
- **No shared skeletons:** the retail skyriding drakes (mount type 402) have no `SKID` chunk. The `retail-native-mounts.md`
  "shared SKELs unsupported" limit does not apply. Highland Drake also has 3 external `.anim` files (AFID).
- **Animation counts:** 12-19 `AdvFly*` animations per model (IDs and names in the assets doc: glide 1532 / slow 1722 /
  fast 1724, dive start 1678 / loop 1530 / end 1588, left 1520 / right 1522, roll 1534 (+ start/end), roll left/right
  1634/1630, flap 1638, flap big 1680, flap up 1702, second flap 1726, slow fall 1704, start 1618, land 1620, idle 1628).
- **Layout check:** passes for Highland Drake, Renewed Proto-Drake, Windborne Velocidrake, Winding Slitherdrake, Grotto
  Netherwing, Algarian Stormrider; **fails for Cliffside Wylderdrake** ("M2 lookup index is out of range").
- **Customization:** the drakes are customizable "companion" models (look chosen through retail drake customization);
  each mount pack needs one fixed default look: geosets + textures from the retail customization tables (not exported
  yet: ChrCustomization* / CreatureDisplayInfoOption).
- **Retail IDs:** animation IDs are above the 3.3.5 AnimationData range, so selection must go by ID on models that have
  the sequence; the AnimationData fallback chain knows nothing about them.

Plan:
1. **Animation override hook.** Find where `CGUnit_C` picks the mount model's sequence while flying
   (`SetKeyBoneSequence`, `ResolveAnimForModel(anim, m_mountModel)`, the movement-driven animation update). Add a
   `UnitScript` hook (like `OnUnitPlotLocalMove`) that lets the extension choose the mount sequence, keeping vehicle /
   spell overrides first.
2. **State -> animation**, from the simulator and the HUD's state:

   | Situation | Animation |
   |---|---|
   | takeoff | AdvFlyStart (Lift Off: FlapBig) |
   | glide | ForwardGlide / Slow / Fast by speed |
   | dive | DownStart -> Down -> DownEnd on recovery |
   | turning | Left / Right by bank |
   | Flap | Flap or FlapUp |
   | Skyward Ascent | FlapBig or SecondFlapUp |
   | Whirling Surge | Roll (Start/End) |
   | stall | SlowFall |
   | landing | AdvFlyLand / LandRun |

   Pick the first ID the model has (`CM2Model` sequence lookup), fall back to stock Fly animations, and keep it
   render-only.
3. **WotLK mounts (no AdvFly):** keep the stock fly loop; add render-only pitch along the flight path (the extension
   already rolls the mount model in `OnUnitWorldTransformUpdated`; add pitch the same way). Optionally use the stock
   glide/hover animation when gliding.
4. **Mount packs:** stage one drake first (Renewed Proto-Drake 4278602 is small and passes) with
   `Tools/RetailAssets/native_mount.py`, the same way as `mastercraft-gravewing` (`docs/retail-native-mounts.md`). Then
   add it to `esteria_skyriding_mounts` if its spell lacks a flight-speed aura. Fix the Wylderdrake layout error
   separately.
5. **Remote riders** need the RiderState message (step 1.6) to play the same animations.
6. **Rider pose:** retail riders lean with the mount; not mapped yet (look for rider anim kits on
   `Mount.MountSpecialRiderAnimKitID`).

## Open items carried over

- **HUD in game:** confirm placement and sizes from a screenshot. No icon masking in this client (square icons); vigor
  flipbooks unused (static fill).
- **Remote banking / pitch:** not shown on other players.
- **Not implemented:** ground skimming, Bronze Timelock, Second Wind.
- **Simulator after a shortened step:** it runs slightly ahead when a collision shortens a step.
- **Cold Weather Flying:** still required in Northrend.
- **Commit:** both repos' work, once the user approves.
