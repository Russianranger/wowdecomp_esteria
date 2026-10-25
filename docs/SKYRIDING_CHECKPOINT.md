# Skyriding: implementation checkpoint

**Handoff: start with `SKYRIDING_HANDOFF.md`** (state, deploy, lessons, next steps for effects and mount animations).

Living status for the work in `docs/Esteria_Skyriding_Implementation_Guide.md`. Update this file whenever a step
starts, lands, or changes plan.

Repos: client `wowdecomp` (branch `Esteria`), server `EsteriaWoW` (AzerothCore, branch `main`).

## Status at a glance

| Milestone | State | Notes |
|---|---|---|
| M0 audit | done | `SKYRIDING_CLIENT_MAP.md`, `SKYRIDING_SERVER_MAP.md`, `SKYRIDING_ARCHITECTURE.md`, `SKYRIDING_TARGET_SPEC.md` |
| M1 shared physics | done, tested | `Engine/Source/Skyriding/*.h`; `Tools/SkyridingTests` passes (15 groups, incl. envelope) |
| M2 local client | built, not run | core hook `UnitScript::OnUnitPlotLocalMove` + `Extensions/Skyriding` |
| M3 worldserver | built, not run | `modules/mod-esteria-skyriding`, opcode entries, anticheat exemption |
| M4 networking | written | `SKYRIDING_PROTOCOL.md`; native heartbeats @10 Hz + observer speed |
| M5 abilities | written | Lift Off, Surge Forward, Skyward Ascent, Whirling Surge, Aerial Halt, Flap; charges and Vigor profiles |
| M6 presentation | partly | HUD addon (built in), speed FOV, render-only banking. Not done: per-model animation profiles, remote banking |
| M7 calibration | not started | needs in-game telemetry; all constants are `tuning-only` |

**Nothing has been run in-game yet.** Builds: client `bin/Win/WowClientRetailInventory.exe` (build/retail-satchel)
compiles clean; server image `esteria-skyriding:20261025` builds clean (module compiled in).

## What exists

Client (wowdecomp):
- `Engine/Source/Skyriding/SkyridingPhysics.h`, `SkyridingResources.h`, `SkyridingWire.h`, `SkyridingEnvelope.h`
  (shared, header-only). The envelope test flies 200 simulated honest riders with 30-150 ms packet jitter (zero
  failures) and catches steady climb, hover, speed hack and a stretched client clock.
- `WoW/Source/Object/Movement.cpp` `CMovement_C::ApplyMovement`: calls `sExtensionMgr->OnUnitPlotLocalMove` for the
  active mover after the native plot; a script may replace the displacement (anchors reset after, like an
  interpolated step). Hook declared in `UnitScript.h`, dispatched in `UnitScript.cpp`, `ExtensionMgr.h`.
- `Extensions/Skyriding/src/Skyriding.cpp`: controller, protocol, Lua API (`GetSkyridingInfo`, `GetSkyridingDebug`,
  `GetSkyridingCooldown`, `UseSkyridingAbility`, event `SKYRIDING_UPDATE`), CVars `skyridingEnabled`,
  `skyridingStyle`, `skyridingFov`, console command `skyriding [steady|dynamic]`.
- `Addons/EsteriaSkyriding/EsteriaSkyriding.lua`: HUD (charge pips, speed bar, ability buttons, style toggle,
  `/skyriding debug|hud|keys|steady|dynamic`); action-button 1-5 keys cast the abilities while skyriding. Embedded via
  `WoW/Source/UIUtil/CMakeLists.txt` + `BuiltinAddOns.cpp` (saved variable `EsteriaSkyridingDB`).

Server (EsteriaWoW):
- `src/server/shared/Skyriding/*.h`: verbatim copies of the shared headers.
- `Opcodes.h/.cpp`: `CMSG_ESTERIA_SKYRIDE` 0x527, `SMSG_ESTERIA_SKYRIDE` 0x528, `NUM_MSG_TYPES` 0x529;
  `WorldSession::HandleEsteriaSkyrideOpcode` (drain only) in `MovementHandler.cpp`.
- `modules/mod-anticheat/src/AnticheatMgr.cpp`: skips players with `CustomData["EsteriaSkyriding"]`.
- `modules/mod-esteria-skyriding/`: scripts, `conf/mod_esteria_skyriding.conf.dist`,
  `data/sql/db-world/esteria_skyriding_mounts.sql`.

## Decisions (and why)

1. **Authority: server-validated client prediction**, not a server re-simulation. The server owns eligibility,
   lifecycle, charges and cooldowns, and checks each airborne movement packet (`SkyridingEnvelope.h`): a budget that
   bounds the rider's energy `v^2/2 + g z` grows only by approved ability credit plus 25/s drift and follows the
   measured energy down; one sample may read 300 above it (noise, never accumulates); speed cap; the client's clock may
   not gain on the server's; no hovering below 3 yd/s for 2.5 s (except Aerial Halt). Violations near-teleport the rider to the last good sample. *Why:* AzerothCore vmaps lack the client's
   M2/WMO collision, so a server re-simulation would rubber-band near geometry. Deviation from guide 7.3.
2. **Remote riders use native extrapolation** plus server-sent `SMSG_SPLINE_SET_FLIGHT_SPEED`; works for stock clients.
3. **Momentum without input:** the extension sets `UNIT_FLAG2_FORCE_MOVEMENT` (0x40) locally while airborne.
4. **Two opcodes with a subtype byte**, protocol version 1.
5. **Profile:** retail 11.2.7 shared charges by default, Dragonflight Vigor by config. Constants are tuning-only and
   come from the server config (sent in Begin).
6. **Abilities are protocol commands, not spells**, with a FrameXML HUD.

## Deploy steps (done 2026-10-25 03:10)

1. Client: back up `G:\3.3.5a - Dev\Wow.exe` to `_backups\skyriding-<time>\`, copy
   `bin\Win\WowClientRetailInventory.exe` over it.
2. Server: apply `esteria_skyriding_mounts.sql` to the world DB (the image runs with `AC_UPDATES_ENABLE_DATABASES=0`),
   copy `mod_esteria_skyriding.conf.dist` to `env/dist/etc/modules/mod_esteria_skyriding.conf`, point
   `docker-compose.twoforms.yml` at `esteria-skyriding:20261025`, recreate `ac-worldserver`.

## In-game test checklist

- [ ] Mount a flying mount in a flyable zone: HUD appears with 6 pips; `skyriding` console line says authorized.
- [ ] Lift Off (action key 1) launches; release all keys: the mount keeps gliding and slowly sinks.
- [ ] Dive gains speed (speed bar turns blue above 714%), climb trades it away, stall drops the nose.
- [ ] Surge / Skyward Ascent spend a pip each; pips refill (12 s, 4 s when fast).
- [ ] Space in the air flaps; Aerial Halt stops you briefly.
- [ ] Touching the ground lands you; you can ride on.
- [ ] `/skyriding steady` gives stock flight; `/skyriding dynamic` back.
- [ ] A second client sees the rider move smoothly.
- [ ] Dismount in the air: normal fall. Fly into a no-fly area: skyriding ends.
- [ ] Server log has no "Skyriding: ... failed" lines during honest flight (tune tolerances if it does).

## Known gaps / risks

- If a collision shortens a step, the simulator is advanced a little further than the time actually flown (the
  remainder is simulated again). Small; revisit if wall slides feel fast.
- `m_unitFieldFlags2` is a descriptor mirror; the extension re-asserts the force-forward bit each frame.
- Remote riders show the native flying pose with the rider's view facing; no remote banking yet.
- No per-mount animation profiles (Native/Adapted/Fallback); mounts play their stock fly animations.
- Ground skimming, Thrill of the Skies visuals and Bronze Timelock not implemented.
- The observer speed and validation run on every airborne packet; not load-tested.

## Log

- 2026-10-25: retail assets mapped (`SKYRIDING_RETAIL_ASSETS.md`). HUD rebuilt with retail art read as
  `retail\<FileDataID>.blp` (no alias rows, so pack installs cannot drop them): vigor pips (background, rising fill,
  spark, frame, flash, wing decor), retail action-bar icon frames and ability icons, cooldown swipes, slot 2 = Skyward
  Ascent in the air / Lift Off on the ground, slot 5 = Switch Flight Style. Speed bar is now opt-in (`/skyriding speed`).
  Icons and widget sheets prefetched into the client's CDN cache. Not yet seen in game; layout offsets are estimates.
- 2026-10-25: flying was refused in Dalaran (AreaTable no-fly flag), so skyriding never began there. Added
  "fly everywhere": the server module (`Skyriding.FlyEverywhere = 1`) and the client extension
  (`SkyridingClientDB`) mark every open-world area flyable and clear its no-fly flag in memory at load; instances are
  unchanged. Server logged "flying opened in 65 areas". Redeployed client and image `esteria-skyriding:20261025b`.
- 2026-10-25 03:10: deployed. Client backup `G:\3.3.5a - Dev\_backups\skyriding-20261025-030229\Wow.exe`
  (previous `docker-compose.twoforms.yml` image: `esteria-twoforms:20261024g`). World DB has
  `esteria_skyriding_mounts`; module config in `env/dist/etc/modules/`. Worldserver up on image `60c59f45`
  ("Skyriding: 0 mount rows, all flying mounts on"). Note: a cancelled `docker build` kept running in BuildKit and
  tagged first; check the container's image id after rebuilding.
- 2026-10-25: M0 recon, architecture. M1 physics + tests pass. Client hook, extension, HUD, banking, FOV built.
  Server opcodes, module, anticheat exemption written. Envelope moved into a shared header after tests showed the
  first version let per-sample slack compound (a steady climb passed); fixed and tested. Server image built.
