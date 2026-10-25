# Skyriding physics

Code: `Engine/Source/Skyriding/SkyridingPhysics.h` (shared with the server). Tests: `Tools/SkyridingTests`.

```bash
cmake -S Tools/SkyridingTests -B build/skyriding-tests
cmake --build build/skyriding-tests --config Release
build/skyriding-tests/Release/skyriding_tests.exe
```

## Model

State: world position, world velocity `v`, boost (accel, seconds left), hover seconds. Input: the rider's view
direction `D = Direction(facing, pitch)`. Per step (semi-implicit Euler, substeps of at most 20 ms):

1. **Steer:** rotate the path `d = v/|v|` toward `D` by `alpha = 1 - exp(-k dt)`, `k = turnResponse / (1 + |v|/turnSpeedRef)`.
   Only the direction changes, never the speed. Pulling the nose *up* is scaled by
   `clamp((|v| - stallSpeed) / (minGlideSpeed - stallSpeed))`, so a stalled rider cannot climb.
2. **Gravity:** `-g z` (`g = 19.29`), scaled by `haltGravityScale` during Aerial Halt's hover.
3. **Lift:** along the path normal in its vertical plane, `min(g * n.z * liftEfficiency, g/minGlideSpeed^2 * |v|^2)`.
   It cancels gravity's pull across the path when fast, so the path holds and only gravity's component *along* the
   path changes speed: dives accelerate, climbs trade speed for height. `liftEfficiency < 1` makes a level glide sink.
   Below `minGlideSpeed` the lift runs out and the path droops: the stall.
4. **Drag:** `-(dragLinear |v| + dragQuadratic |v|^2)` along the path, plus `overspeedDrag * (|v| - maxAirSpeed)` once
   no boost is running. Hard cap `maxBoostSpeed`.
5. **Boost:** `boostAccel` along the path while `boostSeconds > 0`.

Gravity is applied once, to the velocity; nothing integrates a separate vertical speed.

**Energy:** with `E = |v|^2/2 + g z`, steps 1-4 never raise `E` (rotation and lift are perpendicular to `v`, drag and
caps remove it). Only `Apply(Command)` adds energy, bounded by `EnergyGain(command)`. The test checks this on random
inputs; the server's envelope check is built on it.

## Commands

| Command | Effect |
|---|---|
| LiftOff | airborne; `vz = max(vz,0) + liftOffUp`, plus `liftOffForward` along the view |
| SurgeForward / WhirlingSurge | boost `surgeAccel` for `surgeSeconds` / `whirlAccel` for `whirlSeconds` (refresh, no stacking) |
| SkywardAscent | `vz += ascentImpulse` |
| Flap | `vz += flapImpulse` |
| AerialHalt | `v *= haltFactor`, ends boost, hover (`haltGravityScale`) for `haltSeconds` |

## Constants

All `tuning-only` (none observed from retail). Defaults are in `Profile`; the server can override every one from
`mod_esteria_skyriding.conf` and sends them in BEGIN, so client and server always use the same numbers.

| Behaviour (test) | Result with defaults |
|---|---|
| 60 deg dive from 30 yd/s, 3 s | > 55 yd/s, settles near 67 yd/s |
| 45 deg climb from 50 yd/s, 1.5 s | > 30 yd gained, < 35 yd/s left |
| level glide from 40 yd/s, 5 s | sinks 0.05-10 yd/s |
| 80 deg turn | slower at 60 yd/s than at 20 yd/s, under 5 s |
| 30 vs 144 fps, 5 s with surge | positions within 2 % of path length |

## Not done

- Ground skimming, Thrill of the Skies visuals, Bronze Timelock.
- Calibration against retail footage (M7).
