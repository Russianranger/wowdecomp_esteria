# Retail race customization: client UI, API and server persistence

Goal: a retail race behaves 1:1. The player picks every retail option (hair, beard, mustache, sideburns,
eyebrows, eye color, makeup, ears ...), the full choice list is stored by the server, and every other
player sees it.

## What already exists (verified)

| Piece | State |
| --- | --- |
| Retail data | `Db2.*`/`Appearance.*` read ChrModel, options, choices, elements, layers and sections from CASC. Option and choice names decode (`OptionName`, `ChoiceName`). |
| Model, skeleton, textures | Retail Human loads in character select/create; body, hair and eye textures compose in memory (`Compose.*`, `RetailRace.cpp`). |
| Five stock selectors | Skin, Face, Hair Style, Hair Color, Facial Hair drive the matching retail options by name, with retail counts. |
| Glue UI for extra options | `CharacterCreate.lua` already draws up to 29 option rows from `CycleCharCustomization("EA_GET", i)` -> `value, count, label`, with `EA_CYCLE`, `EA_SET`, `EA_CHOICES`, `EA_RANDOM`, `EA_PREVIEW_*`. The native side is `EsteriaCycleCommand` in `EsteriaAppearance.cpp`. Lua gates it with a hard-coded race list, `EA_ACTIVE()`. |
| Server transport for extras | Creation trailers (`HRC1`), roster trailers (`HXE1`/`HXE2`), `extraAppearance BIGINT`, and unit/object padding words (64 bits). Custom opcodes `CMSG/SMSG_ESTERIA_INVENTORY` 0x521/0x522 show the pattern. |

64 bits cannot hold a retail look. A Human male has 13 options with up to 58 choices each, and some races have
more than 20 options. The full list must be stored as choice IDs and sent as its own message.

## Design

A **retail look** is an ordered list of `ChrCustomizationChoice` IDs, one per option of the character's
ChrModel (race + sex). Everything else is derived from it by the recipe code already in the client.

### Client

1. **State.** Each component keeps its option indices (`std::vector<uint32_t>`, one per option) in a map keyed
   like `g_extraFields`. `Choices()` in `RetailRace.cpp` uses that state when it exists, otherwise derives it
   from the five stock bytes.
2. **Native commands.** `RetailRaceCycleCommand(L, character, command)` is called first in
   `EsteriaCycleCommand` and answers the existing `EA_*` contract for retail-mapped races:
   `EA_GET(i)` -> index, choice count, option name; `EA_CYCLE`, `EA_SET`, `EA_CHOICES`, `EA_RANDOM`,
   `EA_PREVIEW_SAVE/RESTORE/END`. Add `EA_CHOICE_NAME(i, j)` so the picker can show real names.
3. **Lua.** `EA_ACTIVE()` and the label/name tables in the patched `CharacterCreate.lua` (inside
   `patch-Z.MPQ` and `patch-enUS-Z.MPQ`) switch to a native query, `EA_RETAIL()`, so any mapped race gets the
   expanded panel. Option rows beyond 29 are not needed yet.
4. **Rules.** Honour `ChrCustomizationReq` / `ChrCustomizationVisReq` (class, unlocks, options that only apply
   to some choices). The element table already hides combinations through related choices.
5. **Network.** `Retail::Appearance` store per GUID. Creation sends the list; the roster and in-world units
   fetch it (below).

### Server (EsteriaWoW core, `modules/mod-custom-server`)

1. **Data.** Export the retail tables once from the same DB2 reader into SQL: `retail_chr_model(race, sex,
   chr_model_id)`, `retail_chr_option(id, chr_model_id, order, name)`, `retail_chr_choice(id, option_id, order,
   name, req_id)`. This is the validation source; no DB2 parsing on the server.
2. **Storage.** `character_retail_look(guid, race_version, choices VARBINARY(256))` - u32 choice IDs. Keep
   writing sensible values into the five stock bytes (`skin`, `face`, ...) so tools, bots and non-retail clients
   still see something.
3. **Create.** New trailer `RCC1` (magic, count, u32 choices) after the existing creation payload. Validate that
   each choice belongs to an option of the race/sex model, one per option, requirements met, then save in the
   creation transaction. No trailer -> default look (first choice of each option).
4. **Roster.** Trailer `RCE1` on `SMSG_CHAR_ENUM`: count, then per character GUID + count + choices.
5. **In-world.** `CMSG_ESTERIA_RETAIL_LOOK` / `SMSG_ESTERIA_RETAIL_LOOK` (0x523 / 0x524). The unit padding word
   carries a look revision so the client knows a retail look exists; the client queries unknown GUIDs and the
   server answers from the loaded `Player`. Changes (barber shop, `.customize`) push the new list to nearby
   players.
6. **Bots/NPCs.** Playerbots take the same validation and default-look path; NPC retail displays are a separate
   (CreatureDisplayInfo) job.

### Order of work

| Phase | Scope | Check |
| --- | --- | --- |
| 1 | Client state + `EA_*` retail commands + Lua gate. Local preview only. | Creation screen lists every option; cycling, dropdown names, random, hover preview all change the model. |
| 2 | Server tables, SQL export, create/save/load/validate, `RCC1`, `RCE1`. | Create a retail character with non-default options; relog; roster shows it. |
| 3 | In-world: padding revision, query opcodes, per-GUID cache, apply to other players and self. | Two clients see each other's full look; changes propagate. |
| 4 | Barber shop, requirement rules, equipment geosets (helm/shoulders/tabard/cape), more races. | Per race acceptance. |

### Risks and decisions

- **Deployment.** Phase 1 needs a GlueXML patch in both MPQs (root and `enUS`), with the usual backups; phase 2
  and 3 need a core rebuild in Docker and a DB migration in `pending_db_characters`.
- **Opcode IDs.** 0x523 and 0x524 are free in `Opcodes.h` (`NUM_MSG_TYPES` is 0x523); bump it.
- **Existing races.** Esteria's own races keep their DLL-era catalogs until each is migrated; the mapping is
  per race (`WOW_RETAIL_RACES` / `RetailRaces.csv`).
- **Licence.** `Db2.*` and `Appearance.*` derive from GPL-3.0 code (WarcraftXL wxl-db2).
