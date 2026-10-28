# Skyriding protocol (version 1)

Code: `Engine/Source/Skyriding/SkyridingWire.h` (shared with the server). Little endian, written field by field. Every
payload starts with a subtype byte; a reader rejects unknown subtypes, out-of-range enums and trailing bytes.

| Opcode | Name | Direction | Status |
|---|---|---|---|
| 0x527 | `CMSG_ESTERIA_SKYRIDE` | client -> server | `STATUS_LOGGEDIN`; consumed by the module in `CanPacketReceive` |
| 0x528 | `SMSG_ESTERIA_SKYRIDE` | server -> client | handled by `Extensions/Skyriding` |

`NUM_MSG_TYPES` 0x527 -> 0x529. Audit: 0x51F/0x520 are TC9 redirect, 0x521..0x526 Esteria; 0x527/0x528 were unused in
both the server opcode table and the client.

## Client -> server

| Subtype | Payload | When |
|---|---|---|
| 1 Hello | `version u8, style u8` (0 dynamic, 1 steady) | entering the world. A client that never says hello never skyrides. |
| 2 Ability | `command u8, sequence u16` | key press; command values = `Skyriding::Command` |
| 3 Style | `style u8` | flight-style toggle |

## Server -> client

| Subtype | Payload |
|---|---|
| 1 Begin | `version u8, fieldCount u8 (23), resources, 23 x f32 Profile` |
| 2 End | `reason u8` (1 dismount, 2 area, 3 teleport, 4 death, 5 style, 6 disabled, 7 vehicle) |
| 3 AbilityResult | `command u8, sequence u16, result u8, cooldownMs u32, resources` |
| 4 Resources | `resources` (sent when a charge regenerates) |
| 5 Correction | none: the server just near-teleported the rider to its last accepted point; zero the velocity |

`resources` = `profile u8 (0 charges, 1 vigor), charges u8, maxCharges u8, nextChargeMs u32`.

## Position

Not in this protocol. The rider's position goes in native `MSG_MOVE_*` packets (heartbeat every 100 ms while
airborne). The server checks each one against the energy envelope (`SKYRIDING_ARCHITECTURE.md`) and tells observers
the rider's speed with native `SMSG_SPLINE_SET_FLIGHT_SPEED`, so stock clients extrapolate skyriders correctly.

## Failure behaviour

- Bad or unknown payload: dropped, logged once per session on the server.
- Ability spam: the server answers every request but rate-limits to 20 per second; extra requests are dropped.
- Unsupported client: no hello, so the mount stays a stock mount.
