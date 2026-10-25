# Skyriding architecture

```text
 client (wowdecomp)                                         server (EsteriaWoW)
 ------------------                                         -------------------
 keys/mouse -> native input -> m_facing/m_pitch (desired)
 CMovement_C::ApplyMovement
   PlotUnitMovement (turn/pitch only)
   UnitScript::OnUnitPlotLocalMove  --> Skyriding extension:
        fixed 50 Hz SkyridingPhysics::Step (shared header)
        returns displacement
   CollideRequestMove (engine collision)
 native MSG_MOVE_HEARTBEAT @10 Hz  ------------------------> MovementHandler -> OnPlayerMove
                                                            mod-esteria-skyriding: energy envelope check
                                                              violation -> NearTeleportTo last good point
                                                            observers <- SMSG_SPLINE_SET_FLIGHT_SPEED
 CMSG_ESTERIA_SKYRIDE (hello / ability / style) ----------> CanPacketReceive: capability, charges, cooldowns
 SMSG_ESTERIA_SKYRIDE (begin / end / ability result) <----- mount aura apply/remove, map change, death, logout
```

## Ownership

| Mode | Who moves the player | Who decides it |
|---|---|---|
| Vanilla / Steady | native client movement, stock server | stock |
| Skyriding grounded | native ground movement (mount is a normal mount) | server sent BEGIN |
| Skyriding airborne | client shared simulator through engine collision; server validates every packet | server: BEGIN/END, abilities |

Transitions are idempotent: BEGIN while active refreshes the profile, END while inactive is ignored, and the client
drops to vanilla on any teleport, map change, mount loss or disconnect without waiting for the server.

## Coordinates and units

World yards and seconds. x north, y west, z up; facing 0 = +x, counter-clockwise; pitch up positive.
Direction(facing, pitch) = (cos p cos f, cos p sin f, sin p). Gravity 19.29 yd/s^2 (the client's fall constant).

## Shared code

`Engine/Source/Skyriding/SkyridingPhysics.h` and `SkyridingWire.h` are header-only and copied verbatim to the server
(`src/server/shared/Skyriding/`). `Tools/SkyridingTests` checks them.

## Rollback

Client: `skyridingEnabled 0` CVar, or build with `-DWOWDECOMP_EXTENSION_SKYRIDING=OFF` (the core hook is inert without a
script). Server: `Skyriding.Enable = 0` in `mod_esteria_skyriding.conf`, or disable the module.
