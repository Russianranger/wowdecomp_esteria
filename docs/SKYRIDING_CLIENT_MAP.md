# Skyriding: client source map (wowdecomp, branch Esteria)

Verified by reading the code on 2026-10-25. Symbols, not line numbers.

## Input -> movement events

| Step | Symbol | File | Notes |
|---|---|---|---|
| Keys to move flags | `CGInputControl::MovePlayer` | `WoW/Source/UIUtil/InputControl.cpp` | `direction` is forced to 1 when `unit->m_unit->m_unitFieldFlags2 & 0x40` (UNIT_FLAG2_FORCE_MOVEMENT); with no direction it calls `OnMoveStopLocal` once. **Used to keep forward momentum while airborne.** |
| Jump / ascend key | `CGUnit_C::TryJumpOrAscend` -> `CMovement_C::OnJumpLocal`; ascend/descend keys -> `CGUnit_C::OnAscendDescendStartLocal` | `WoW/Source/Object/ObjectClient/Unit_C.cpp`, `InputControl.cpp` | While flying the jump key sets `MOVEMENTFLAG_ASCENDING`; the controller reads it as the flap button. |
| Mouse / keyboard steering | `CMovementShared::SetFacing`, `SetPitch`, turn/pitch flags | `WoW/Source/Object/MovementShared.cpp` | `m_facing`/`m_pitch` are the player's *desired* direction for the controller. |
| Event queue | `CMovement_C::AddPlayerMoveEvent` -> `m_moveQueue`; drained by `CMovement_C::UpdatePlayerMovement` | `Movement.cpp` | Links the mover into `CMovementGlobals::m_movers`. |

## Per-frame movement

| Step | Symbol | Notes |
|---|---|---|
| All movers | `CMovement_C::MoveUnits` -> `ExecuteMovement(time, lastTime)` | clamps a frame to 250 ms (`SkipTime` for the rest). |
| Step | `CMovement_C::ApplyMovement(time, elapsed)` | runs only while `m_flags & 0xC010FF` (or hover). Off spline it calls `PlotUnitMovement(GetMoveStartTime(step), &move)` (anchor + speed * t along facing/pitch; also integrates turn/pitch into `m_facing`/`m_pitch`), then `RequestMove`. **Hook point:** after the plot, the skyriding script replaces `move`. |
| Collision | `CMovement_C::RequestMove` -> `CollideRequestMove(time, elapsed, delta)` | for the active mover always collides. With `CanCollideWhileFlying()` the 3D path is `CMovement_C::Swim`. |
| Anchors | `CMovementShared::UpdateAnchors(force)` | anchor = current position; native code does this after an interpolated step. The hook path does the same. |
| Heartbeat | `ExecuteMovement` sends `MSG_MOVE_HEARTBEAT` when `cursor >= CMovementGlobals::m_timeCounterMaybe`; `UpdateHeartbeatTimerA` sets it to `time + 500`. | The controller lowers it to 100 ms while airborne. |

## State and flags

- `CMovementShared` fields: `m_position`, `m_facing`, `m_pitch`, `m_flags` (`MovementFlags`, `WoW/Source/Object/Types.h`),
  `m_flightSpeed`, `m_anchor*`.
- `MOVEMENTFLAG_CAN_FLY` 0x01000000 (server grants), `MOVEMENTFLAG_FLYING` 0x02000000, `MOVEMENTFLAG_ASCENDING` 0x00400000,
  `MOVEMENTFLAG_DESCENDING` 0x00800000.
- Local unit: `ClntObjMgrGetActivePlayer()`, `CGUnit_C::s_activeMover`, unit's mover `CGUnit_C::m_movementData`
  (`CMovement_C`), mount `CGUnit_C::m_mountDisplayId`.
- Start / stop flight: `CMovement_C::OnFlightLocal(eventTime, flying)` queues `MOVEEVENT_SET_FLYING`/`UNSET_FLYING`.

## Packets

- Outgoing movement: `CGUnit_C::SendMovementUpdate` -> `CMovement_C::WriteMovementStatusToPacket` -> `GetMoveStatus`.
  `UnitScript::OnUnitMovementMessage` observes the finished message.
- Custom server messages: `ExtensionMgr::RegisterMessageHandler(opcode, ...)` (only for opcodes the stock table does not
  handle); sends: `ExtensionMgr::SendClientMessage(opcode, bytes, size)`. Existing Esteria opcodes 0x521..0x526.

## Extension seams used

- `Extensions/<Name>/src/*.cpp`, entry `Add<Name>Extension()` (generated loader, `Extensions/CMakeLists.txt`).
- `ClientScript::OnEnterWorld/OnLeaveWorld`, `WorldScript::OnWorldUpdate`, `UnitScript` (new hook
  `OnUnitPlotLocalMove`), `ExtensionMgr::RegisterLuaFunction`, `RegisterFrameScriptEvent`, `RegisterCVar`,
  `RegisterConsoleCommand`.

## Animation / camera (M6, not yet traced in depth)

- Flying pose and pitch come from the native flying flags; camera flight handling in `WoW/Source/UIUtil/Camera.cpp`
  (references `MOVEMENTFLAG_FLYING`).
- Model transform: `UnitScript::OnUnitWorldTransformUpdated` fires after the model gets its world transform.

## Unknowns

- Whether `Swim` clears flying on ground contact (controller lands itself regardless).
- Camera FOV field and safe write point (M6).
