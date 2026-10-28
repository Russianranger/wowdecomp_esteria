# Skyriding target spec

| Item | Choice |
|---|---|
| Reference | Retail patch 11.2.7 "Skyriding" (six shared charges for Surge Forward and Skyward Ascent). Dragonflight 10.x Vigor is a selectable profile. |
| Claim | "high-fidelity recreation", not retail-identical. No constant below is a Blizzard number. |
| Flight styles | Dynamic (skyriding) and Steady (stock 3.3.5 flight). Per-character preference, client CVar `skyridingStyle`, sent to the server. |
| Supported mounts | Every mount aura with `SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED` when `Skyriding.AllFlyingMounts = 1`, plus rows in `esteria_skyriding_mounts` (which can also disable a mount). |
| Maps | Wherever the server already allows flying (`Player::CanFly`-style area check, `mod-fly-anywhere` respected). Never in battlegrounds, arenas or instances. |
| Clients | Only clients that sent the capability hello (`CMSG_ESTERIA_SKYRIDE` HELLO, version 1). Others get stock flight. |

## Abilities (first wave)

| Ability | Effect | Cost (charges profile) | Cost (vigor profile) |
|---|---|---|---|
| Lift Off | ground -> airborne, strong up + forward impulse | 1 charge | 1 vigor |
| Surge Forward | forward acceleration for 1.0 s | 1 charge | 1 vigor |
| Skyward Ascent | upward impulse, nose up | 1 charge | 1 vigor |
| Flap (jump key in air) | small upward impulse, 0.6 s cooldown | free | free |
| Whirling Surge | strong forward acceleration, 30 s cooldown | free | free |
| Aerial Halt | brakes to a near hover, 6 s cooldown | free | free |

Charges: 6, one regenerates every 12 s, every 4 s above the Thrill of the Skies speed. Vigor: 6, regenerates only on
the ground (one per 5 s) or above Thrill speed (one per 15 s). All `tuning-only`.

## Intentional Esteria deviations

- Server validates a physical envelope instead of re-simulating (see `SKYRIDING_ARCHITECTURE.md`).
- Abilities are not spells: they are protocol commands with a FrameXML HUD and keybindings.
