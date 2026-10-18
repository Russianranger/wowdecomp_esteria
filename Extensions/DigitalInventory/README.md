# DigitalInventory

Native client transport for the matching EsteriaWoW `DigitalInventory` core subsystem.
Enable with `WOWDECOMP_EXTENSION_DIGITALINVENTORY=ON`; extension discovery includes it automatically.
The paired Lua addon lives in `EsteriaWoW/client-customization/EsteriaInventory`.

The addon exposes a searchable, five-column carried-inventory window. The client renders twenty
pooled item buttons and receives a separate aggregate item-count/tool index. `CGBag_C` and spell
reagent/tool checks include this index only for the active player's carried inventory.
No player/container update fields, bag ceilings, or original opcode values change.

## Wire protocol v1

Little-endian integers, NUL-terminated UTF-8 strings. Client request opcode `0x521`, server snapshot
opcode `0x522`; the matching server's opcode ceiling is `0x523`. Stock server/client combinations
do not support this extension. The addon first uses the stock addon-whisper channel:
`ESTERIADI\tHELLO:1` to the player, answered by the matched core as `ESTERIADI\tREADY:1`.
An old core's unchanged HELLO echo does not authorize custom traffic. No inventory opcode is sent
until READY is received from the player's own whisper channel. Ordinary bags remain available
until an active native handshake succeeds.

Request fields, in order:

| Field | Type |
|---|---|
| version, action | uint8 each |
| request ID, expected revision, selected item low GUID, count, page offset | uint32 each |
| merchant target GUID | uint64 |
| category, sort | uint8 each |
| search (maximum 80 bytes) | string |

Actions 0..11: browse, deposit, deposit all, withdraw, split, merge, expand, favorite, activate,
use, equip, sell. For expand, count carries the price quoted to the user. Mutations require
the current revision and a fresh monotonic request ID; clients do not retry timed-out mutations.

Snapshot fields, in order:

| Field | Type |
|---|---|
| version | uint8 |
| request ID (0 for push), revision, capacity, used virtual positions, filtered row total, offset, upgrade price, upgrade positions | uint32 each |
| active, status | uint8 each |
| message | string |
| visible row count (maximum 255) | uint8 |
| rows | repeated row |
| aggregate entry count (maximum 10,000) | uint16 |
| item ID, virtual count, totem category | repeated uint32 triplets |

Each row: GUID/item ID/count (uint32), digital/bag/slot/quality/favorite/category/inventory type/soulbound
(uint8), current/max durability (uint32), item hyperlink/name/icon (strings).
Bag and slot fields use server coordinates. The Lua bridge converts physical bag 255 slots 23..38 to
bag 0 slots 1..16, and bags 19..22 slots 0..253 to bags 1..4 slots 1..254.

The reader validates lengths, row counts, categories, physical coordinates and string bounds before
replacing cached state. Count cache is cleared on leaving the character or receiving an inactive response.
The extension calls `CGTradeSkillInfo::RefreshList` after virtual revisions change.

## Native Lua verification

`tests/LuaHarness.cpp` links the same patched `lua.lib` used by the client and executes the real addon
through the companion core's `tools/tests/esteria_inventory_ui.lua` harness. It supplies standalone
profiling-clock, pointer-validation, test-file loading and stdout hooks; it does not launch the game.
Run ordinary, `legacy` and `oldserver` harness modes. FrameXML rendering, controller focus and gameplay
acceptance still require the matched client/server in-game.
