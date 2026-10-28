# Skyriding: server source map (EsteriaWoW / AzerothCore)

Verified by reading the code on 2026-10-25 (`EsteriaWoW` main, `e4d7bee`).

## Movement packets

| Step | Symbol | File | Notes |
|---|---|---|---|
| Dispatch | `WorldSession::Update` calls `sScriptMgr->CanPacketReceive(this, packet)` before `opHandle->Call` | `src/server/game/Server/WorldSession.cpp` | a module can consume a packet. |
| Movement opcodes | `WorldSession::HandleMovementOpcodes` -> `ProcessMovementInfo` -> `VerifyMovementInfo` | `src/server/game/Handlers/MovementHandler.cpp` | broadcasts the client packet to the set (`SendMessageToSet(&data, _player)`). |
| Hook | `sScriptMgr->OnPlayerMove(plrMover, movementInfo, opcode)` (`MovementHandlerScript`) | same | fires before `HandleMoverRelocation`; observe only. |
| Relocation | `WorldSession::HandleMoverRelocation` -> `Unit::UpdatePosition` | same | under-map kill check. |
| Kicks | `sScriptMgr->AnticheatCheckMovementInfo` returns false -> `KickPlayer` | same | no module implements it today. |

## Anticheat

`modules/mod-anticheat`, `AnticheatMgr::StartHackDetection` (called from its `OnPlayerMove`). Already skips GMs, taxi,
transports and vehicles. **Patch:** also skip players whose `CustomData` holds key `"EsteriaSkyriding"`.
`Anticheat.Enabled = 1` in `env/dist/etc/modules/Anticheat.conf`.

## Flight and speed

- `Unit::SetCanFly(bool)` (sends `SMSG_MOVE_SET_CAN_FLY`/`UNSET`), `Unit::SetSpeed(UnitMoveType, rate, forced)`,
  `baseMoveSpeed[]`, `UNIT_FLAG2_FORCE_MOVEMENT` 0x40 (`UnitDefines.h`).
- Observer speed: `SMSG_SPLINE_SET_FLIGHT_SPEED` 0x385 (packed guid + float), sent to the set excluding the mover.
- Flyable area: `PlayerScript::OnPlayerCanFlyInZone` (used by `mod-fly-anywhere`).

## Lifecycle hooks

- `UnitScript::OnAuraApply(Unit*, Aura*)`, `OnAuraRemove(Unit*, AuraApplication*, AuraRemoveMode)`.
- `PlayerScript::OnPlayerLogin/OnPlayerLogout/OnPlayerMapChanged/OnPlayerJustDied/OnPlayerUpdate/OnPlayerBeforeTeleport`.
- `Player::NearTeleportTo` for corrections.

## Opcodes

`src/server/game/Server/Protocol/Opcodes.h/.cpp`: Esteria opcodes 0x521..0x526, `NUM_MSG_TYPES` 0x527. New:
`CMSG_ESTERIA_SKYRIDE` 0x527 (STATUS_LOGGEDIN), `SMSG_ESTERIA_SKYRIDE` 0x528, `NUM_MSG_TYPES` 0x529.
Handler `WorldSession::HandleEsteriaSkyrideOpcode` only drains the packet; the module consumes it in `CanPacketReceive`.

## Per-player data

`Object::CustomData` (`DataMap`, `src/common/Utilities/DataMap.h`): the module stores its per-player state there, so
logout cleanup is automatic and the anticheat can test for it with `Get<DataMap::Base>`.

## Unknowns

- Bots (`mod-playerbots`) never send the hello, so they never skyride.
