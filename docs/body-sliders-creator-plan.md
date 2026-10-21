# Body sliders in character creation, saved per character, visible to everyone

**State (2026-10-21):**
- Phase 1 (client refactor + creator Body page): done and tested by hand. Flow is Appearance -> Next -> Body -> Accept.
- Phase 2 (storage + wire + character-select display): code written on both sides, client builds, wire format unit
  tested, server compile in progress. NOT deployed: the server image and the `character_body_shape` table must go in
  before the new client exe, because the client appends a `BSH1` footer the old server rejects.
- Phase 3 (nearby players) and 4 (hardening): not started.
- Server changes and deploys still need an explicit go-ahead each time.

## Goal

1. A second customization page in the character creator with native sliders for every body/face slider the chosen
   race and sex supports.
2. The values are saved per character on the server.
3. They show on the character-select model.
4. Other players see them in the world.

## What exists today (verified)

- The sliders are local-only: one global state, applied to the active player's model each frame (`Unit_C.cpp` ->
  `BodyShape::Apply`). Bone sliders go through bone matrices. Breast/butt give the player model its own GPU vertex
  buffer (`CM2Model::BindBodyShapeVertices`).
- 38 sliders: 14 body, 22 face, 2 mesh. Which ones apply is per skeleton (`GetBodyShapeSupport`). Face regions are
  keyed by bone-name hash, so they work across races.
- The creator is a custom GlueXML `CharacterCreate.lua` (about 2,900 lines) shipped inside `Data\patch-Z.MPQ` and
  `Data\enUS\patch-enUS-Z.MPQ`. It draws option rows from native `CycleCharCustomization("EA_*", ...)` commands.
  `Tools/RetailAssets/glue/patch_character_create.py` patches it idempotently with backups.
- Precedent for per-character data: the Eye Lightning toggle (commit `0a2311b`, `EyeEffects.h`, server plan
  `EsteriaWoW/.agents/plans/eye-lightning-toggle`):
  - an optional footer appended to CMSG_CHAR_CREATE / CUSTOMIZE / FACTION_CHANGE / RACE_CHANGE;
  - a tail block appended to SMSG_CHAR_ENUM (`HXE1` / `HXE2`) read before stock parsing;
  - nearby-player state carried in a spare update-field bit.
- The existing per-character storage (`characters.extraAppearance`, 64 bits) is almost full: about 48 bits are the
  Retail appearance and bit 63 is eye lightning. 38 sliders do not fit.

## Phase 2 as built

- Shared header `BodyShapeWire.h` (client `Engine/Source/Retail/`, server `src/server/shared/`, byte-identical):
  version byte + 48 signed slot bytes (37 assigned), `BSH1` footer client->server, `BSE1` tail block on SMSG_CHAR_ENUM.
- Client: `BodyShapeCreateFooter` after the EYE1 footer on create (only when a slider is set), customize, faction and
  race change (always, so a reset reaches the server); character-list tail parsed in `EsteriaEnumExtra` into
  `BodyShape::Roster()`; `CCharacterSelection::Idle` applies the selected character's values; paid customization
  starts from the saved values (`BodyShapeLoadExisting`).
- Server: table `character_body_shape (guid, data BINARY(49))` in `data/sql/custom/db_characters/`; statements
  `CHAR_REP_BODY_SHAPE`, `CHAR_DEL_BODY_SHAPE`, `CHAR_SEL_ENUM_BODY_SHAPE`; footer parsing in the four handlers;
  save in the create transaction and in `Player::Customize`; delete with the character; enum query chained after the
  permanent-aura query. The table must exist before the new worldserver starts (statements are prepared at startup).

## Design

### 1. One shared schema (client and server copies must match)
`BodyShapeWire.h`, like `EyeEffects.h`:
- schema version byte;
- the ordered slider list, each value an `int8` in -100..100 (breast/butt 0..100); 0 is default;
- total payload 1 + 38 bytes;
- reject unknown versions, out-of-range values and trailing bytes; unknown or trailing sliders from a newer client
  are dropped.

### 2. Client: make the system per-unit instead of global
- Replace the single global state with values per unit GUID and an "applier" per model (applied-bone set, size,
  region cache, mesh profile, bind pose). Remove an applier when its model is destroyed.
- Apply to any model: the creator preview, character-select models, and every visible player.
- Cost guards for many players: bone sliders are cheap; the breast/butt buffer is about 12 MB per shaped model, so
  build it only for players within a distance cap and release it beyond that.

### 3. Creator: second page
- Page toggle on the creator panel: Appearance / Body. The Body page lists the supported sliders for the current
  race and sex, grouped Body / Face, with Reset and Randomize.
- Native commands (same style as `EA_*`): list, get, set, supported, reset, random. Live preview on the creator
  model. Values persist across race, sex and class changes within the session, and unsupported ones are hidden, not
  discarded.
- Delivered by extending `patch_character_create.py` (dry run by default, backups, refuses while the game runs).
  Needs XML/Lua frames; the existing creator panel style is reused.

### 4. Wire
- Append a `BSH1` footer (schema payload) to create / customize / faction-change / race-change, next to the `EYE1`
  footer, with the same strict validation (absent = defaults, duplicate or malformed = reject).
- Add a `BSE1` block after SMSG_CHAR_ENUM, parsed next to `HXE1/HXE2`: guid + payload per character that has
  non-default values. The character-select model applies it.
- In-world: a new server-to-client opcode `SMSG_BODY_SHAPE` (guid + payload), sent to a player when another player
  becomes visible and when values change; one new client handler. Chosen over new update fields because those would
  change the descriptor table and the server's field mask for every object. (Alternative if you prefer: 10 new public
  PLAYER update fields. More invasive.)

### 5. Server (EsteriaWoW)
- New table `character_body_shape` (guid PK, version, data BLOB(64)). No change to `characters`. Delete with the
  character; copy on customization; keep across race/faction change (unsupported values are ignored client-side).
- `CharacterHandler`: parse the footer in the four handlers; add the `BSE1` block to the enum; load on login; save
  with the player; hook visibility (`Player::UpdateVisibilityOf`) to send `SMSG_BODY_SHAPE`.
- Docker: schema SQL as an update file; image rebuild and worldserver recreate only on your approval, with the same
  rollback tag pattern used for eye lightning.

### 6. Barbershop / later
Not included. Once the table exists, editing in the barbershop is a small extension.

## Phases (each testable on its own)
1. **Client refactor + creator page** (no server): per-unit state, creator page, preview. Test: sliders in the
   creator drive the preview model; nothing saved yet.
2. **Server storage + wire**: SQL, footer parsing, `BSE1`, persistence. Test: create a character, relog, values come
   back at character select and in world.
3. **Nearby players**: `SMSG_BODY_SHAPE`, visibility hook, distance cap. Test: two clients see each other.
4. **Hardening**: random/reset, race and sex change, many players on screen (frame time, memory).

## Risks
- Creator Lua is large and heavily customized; the new page is additive but must not break the existing options.
- The shared schema is a contract: changing slider order or meaning later means a version bump.
- Mesh sliders (breast/butt) on many visible players cost memory; hence the distance cap.
- Masked profiles reshape only the body mesh, not clothing, on every race except Human.
- Server-side validation only checks ranges; whether a race supports a slider is a client rule.

## Decisions to confirm
1. Transport to other players: new opcode (recommended) vs new update fields.
2. Storage: new table (recommended) vs packing into existing fields (does not fit).
3. Whether Phase 1 may also include the character-select preview using local-only values, so you can see the
   creator work before the server exists.

## Retail look sync (full customization for every player)

Added after phase 3, using the same framing. Handoff gap 4 ("option state beyond the five stock bytes is not persisted")
and the in-world gap ("other players' looks only reflect their five bytes") are closed by this.

- `RetailLookWire.h` (client `Engine/Source/Retail/`, server `src/server/shared/`, byte-identical): a 132-byte blob =
  version, race, sex, option count, then 64 u16 choice indices, one per option of the race/sex model (stock bytes
  included). Opaque to the server (structure checked only). Footer `RLK1` (last in create / customize / faction / race
  change, stripped before `BSH1`), tail block `RLE1` on SMSG_CHAR_ENUM, in-world `SMSG_ESTERIA_RETAIL_LOOK` = 0x524.
- Client: `RetailLookCreateFooter` captures the creator's selection; looks are kept by guid and applied by
  `RetailRaceTick` to each component whose owner has one (own character, other players), and at character select and in
  the creator when customizing. Races with the legacy packed codec are skipped (they keep their own channel).
- Server: table `character_retail_look (guid, data BINARY(132))`; same load / save / delete / visibility hooks as the body
  sliders. Characters created before this change have no saved look until they are customized.

