# Two Forms: saving both forms on the server (Phase 3 scope)

**State:** step 1 (server side) is written, uncommitted, and not built; see section 10. The rest is scope only. Client facts are
from this branch; server facts are from `EsteriaWoW` (`main`, `63afc06`).

## 1. What changed since TWOFORMS.md

`TWOFORMS.md` section 3.3 and 4.4 assumed the server has no way to carry a full Retail look. It does: server commit
`e41beda` (21 Oct) added one.

| Piece | Where |
|---|---|
| 132-byte look, `version race sex count + 64 x u16` | `src/server/shared/RetailLookWire.h` (byte-identical with the client copy) |
| Footer `RLK1` on create / customize / faction / race change | `CharacterHandler.cpp` (`ReadTail`, create at ~442, customize ~1965, faction ~2298) |
| Stored in `character_retail_look(guid, data BINARY(132))` | `data/sql/custom/db_characters/`, statements `CHAR_REP/SEL/DEL_RETAIL_LOOK` |
| Enum tail `RLE1` (guid + payload per character) | `HandleCharEnum`, client `EsteriaAppearance.cpp:2134` |
| Sent to the player and to everyone who sees them | `SMSG_ESTERIA_RETAIL_LOOK` 0x524, `Player::SendRetailLookTo` |
| Ownership check helper | `WorldSession::IsLegitCharacterForAccount` |

So persistence is an extension of that scheme, not a new transport.

## 2. Data model

Keep `character_retail_look` meaning "the look the world sees": the look of the **active** form. Everything that reads it
(enum tail, visibility sends, char select) keeps working. Add one table for the other form:

```sql
CREATE TABLE IF NOT EXISTS `character_form_look` (
  `guid` int unsigned NOT NULL,
  `data` binary(132) NOT NULL,   -- the parked form's look, same layout as character_retail_look
  PRIMARY KEY (`guid`)
);
```

Wire change (as built): a look of the default model stays **version 1**, byte for byte as today, so existing rows and clients are
untouched. A look that names a form is **version 2**, with the form (1..4) in the high nibble of the `sex` byte; the size stays
132. A version-1-only reader rejects version 2 and falls back to the five stock bytes instead of misreading it.

Forms (client `app::Form`): 1 Wolf, 2 Human (race 12); 3 Dragon, 4 Visage (race 28). Valid pair per race: 12 -> {1,2},
28 -> {3,4}. A look saved for the default form (Wolf, Visage) behaves exactly as today.

## 3. Option B flow (as requested)

Stock `CMSG_CHAR_CREATE` is untouched. After it succeeds the client commits the pair with one new message.

| Step | Detail |
|---|---|
| Client creates the character | sends the active form's look in the existing `RLK1` footer, as now (so a stock-style character exists immediately) |
| Client finds the new GUID | it is not in `SMSG_CHAR_CREATE`. On the next enum, match the pending commit by name + race + sex |
| `CMSG_ESTERIA_FORM_LOOK` (0x525, `STATUS_AUTHED`) | `guid(8) revision(u4) activeForm(u1) look(132) look(132)` |
| Server validates | `IsLegitCharacterForAccount`; race and sex equal the character cache's; the two forms are the race's pair, active first; each look passes `ReadPayload` with the right form. The revision is only echoed in the ack: the write is an idempotent REPLACE, nothing is stored |
| Server writes | one transaction: `character_retail_look` = active form's look, `character_form_look` = the other |
| `SMSG_ESTERIA_FORM_LOOK_ACK` (0x526) | `guid(8) revision(u4) result(u1)`. Client retries a few times, then gives up and leaves defaults |
| `NUM_MSG_TYPES` | 0x525 -> 0x527 |

Why B is still worth it: one handler also covers barbershop edits, the in-world form toggle (Phase 4), faction/race
change cleanup, and characters made before this change. Its cost is the GUID-discovery step and a small client retry queue.

**Alternative (cheaper for create only):** a second footer `RLF1` carrying the parked look, stripped just before `RLK1`
and written in the same create transaction. Atomic and no GUID step, but it has to be added to four handlers (create,
customize, faction, race change) and does nothing for legacy characters. I would pick B for the single handler and the
reuse; say so if you would rather have atomic create.

## 4. Server work (EsteriaWoW)

1. `RetailLookWire.h`: version 2, form nibble, `Valid`/`ReadPayload` updated; accessor `Form()`. Mirror to the client copy.
2. New SQL: `data/sql/custom/db_characters/character_form_look.sql` plus a `pending_db_characters` revision, like the
   existing tables. Statements are prepared at worldserver start, so apply it first.
3. `CharacterDatabase.cpp`: `CHAR_REP/SEL/DEL_FORM_LOOK`; delete with the character (`Player::DeleteFromDB`).
4. `Opcodes.h/.cpp`, `WorldSession.h/.cpp`: `HandleEsteriaFormLookOpcode` and the ack.
5. Customize / faction / race change: if race or sex changes, delete the parked row (a stale look is ignored today for the
   active one; do the same here). Do not reuse a parked look across sexes.
6. Load the parked look at login so the player can toggle (Phase 4). Until then it is only stored.

## 5. Client work (wowdecomp)

1. `RetailLookWire.h` copy at version 2; `Look` gets `form`.
2. `CaptureLook`/`ApplyLook` take a form (today they return false for a non-default form, marked `shortcut:`), and read
   `g_formSlots`/`g_formMemory` for the parked form.
3. Pending-commit state set after a successful create, consumed on enum when name, race and sex match; send, then retry
   until ack or a short timeout.
4. Rendering from the saved look: `CharacterSelection.cpp:563` and `Unit_C.cpp:7978` call `RetailRaceModelPath(race, sex)` with
   no form. Pass the form from `g_looks[guid]` so a Dragon character loads the Dragon model.
5. Apply the received look's form to the component (`g_form`) before `ApplyLook`.

## 6. Not in Phase 3

- A Dragon in the world: equipment layers do not fit the Dragon sheet (already seen in the creator, which now shows it
  bare), plus animations and particles.
- Toggle rules, spells, and precedence over druid/polymorph/mounts (Phase 4).
- Barbershop UI.

## 7. Acceptance

| Case | Expect |
|---|---|
| Create Dracthyr, Dragon + Visage edited, relog | both looks restored in the creator-equivalent state; char select shows the active form |
| Create Worgen, Wolf + Human edited | same |
| Second client in range | sees the active form's model and look |
| Commit lost (disconnect before ack) | character exists with the active form's look and defaults for the other; no crash, no partial rows |
| Forged guid / wrong race / equal forms / bad count | rejected, nothing written |
| Old client on new server, new client on old server | stock bytes only, no parser errors |
| Customize or race change | parked row removed |
| Delete character | both rows removed |

## 8. Order and size

1 wire + SQL + server handler (one session of work), 2 client capture + commit queue, 3 render by form, 4 test matrix with a
disposable client and a local test realm. Docker image must be rebuilt for the server side; no live realm change is made
without your say.

## 9. Open questions

- B as written, or atomic footer `RLF1` for create?
- Is it acceptable that a character whose commit is lost shows defaults for the other form until the next edit?

## 10. Step 1 status (server)

Written in `EsteriaWoW`, uncommitted, **not compiled** (its `AGENTS.md` says not to build unless asked) and not run:

| Change | File |
|---|---|
| Version-2 look, `form` field | `src/server/shared/RetailLookWire.h` (copied byte-identical into `Engine/Source/Retail/`) |
| New table | `data/sql/updates/pending_db_characters/rev_20261023233000000_character_form_look.sql` |
| `CHAR_REP_FORM_LOOK`, `CHAR_DEL_FORM_LOOK` | `CharacterDatabase.h/.cpp` |
| Opcodes 0x525 / 0x526, `NUM_MSG_TYPES` 0x527 | `Opcodes.h/.cpp` |
| `HandleEsteriaFormLookOpcode` | `CharacterHandler.cpp`, declared in `WorldSession.h` |
| Parked row removed on customize, faction and race change, and on character delete | `Player::Customize` (both handlers call it), `Player.cpp` |

Client side so far: the wire copy, a guard that ignores version-2 looks until step 3 renders by form, and a wire round-trip test
(`RetailAssetTests`, passes). Not done: the commit queue, GUID discovery, form-aware rendering, sending the parked look to the
owner at login.

## 11. Step 2 status (client commit queue)

Built and installed in the dev client, **not exercised against a server** (none of the server code is built yet).

| Piece | Where |
|---|---|
| Capture both forms as version-2 looks (live form from the component, other from its parked slot) | `RetailRace.cpp`, `BuildFormLook`, `RetailFormCommitPrepare` |
| Called as the create packet is built, and with the create reply | `ClientServices.cpp` (`RequestCharacterCreate`, `HandleCharacterCreate`) |
| GUID discovery: each character of the next list is offered by name + race + sex; no match drops the commit | `RealmConnection.cpp` (`HandleCharEnum`), `RetailFormCommitOffer` / `ListDone` |
| Send `CMSG_ESTERIA_FORM_LOOK` 0x525 | `sExtensionMgr->SendClientMessage` |
| Receive `SMSG_ESTERIA_FORM_LOOK_ACK` 0x526 | `Extensions/BodyShapeSync` (next to the look and slider receivers) |
| Resend every 3 s, three attempts, then drop | `RetailFormCommitTick`, called from `CCharacterSelection::Idle` |

Only Worgen and Dracthyr prepare a commit. Progress is written to `Logs/RetailRace.log` ("form commit sent / acknowledged /
dropped"). A new client against a server without the handler gets no ack and drops the commit after three tries; the server
ignores the unknown opcode.

Still open: form-aware rendering from the saved look (step 3), sending the parked look to its owner at login, and an end-to-end
run with a built server.

## 12. Step 3 status (render from the saved look)

Built and installed, **not exercised**: a saved version-2 look only exists once the server stores one.

- `ApplyLook` / `LookApplicable` accept a version-2 look for a Two Forms race: the look's form becomes the component's form
  (`g_form`), its options are that form's, and `SyncBodyModel` swaps the body when the model differs. A default-model look still
  refuses a component the creator has switched to another form.
- `RetailRaceLookForm(guid)` returns the saved form. Character select (`CharacterSelection.cpp`) and world units (`Unit_C.cpp`)
  pass it to `RetailRaceModelPath`, so a Dragon is created as a Dragon when its look is already known (the enum tail puts it
  there before character select). In the world the look usually arrives after the unit's model exists; the body swap covers
  that, with one frame of the default form.
- Dragon form ignores equipped item layers when it composes its body sheet (`BindBody`), as the creator already does: its
  armor is customization choices.

Not done: Dragon-form attachments (helm, shoulders, weapons), animations and particles in the world, the parked look for the
owner at login, and the toggle (Phase 4).
