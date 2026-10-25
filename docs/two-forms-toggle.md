# Two Forms: the in-game toggle (Phase 4, spell-driven)

**State:** plan, then server side written (section 7). The client part is listed but not started. Nothing here is deployed.

## 1. What exists today (verified)

| Thing | Fact |
|---|---|
| Saved forms | `character_retail_look` holds the **active** form's look, `character_form_look` the **parked** one (both version-2 looks, form 1..4). Committed at creation and acknowledged (T1/T2 passed on the test realm). |
| Rendering | Character select and the world pick the model from the active look's form; a new look for a unit already in view swaps its body (`ApplyLook` + `SyncBodyModel`). |
| Dracthyr Visage spell | `110685` "Visage" (`spell_race_visage`, `mod-custom-server/src/race_racials.cpp`): toggles aura `110686`, which transforms into the Chosen Identity race's display (default Human). It never touches the saved forms. |
| Worgen Two Forms | `68994`, `68995`, `68996`, `110020`, all named "Two Forms": apply aura `56` (transform) to NPC `55276`/`55277`. No script. They are donor placeholders. |
| Retail data shape | Dracthyr race row -> `ChrModel 89` (Dragon); Visage (127/128) has no race row: Dragon is native, Visage the alternate. Worgen likewise (wolf native, human alternate; Cata `ChrRaces.AlteredFormRaceID`). |

## 2. Design

The toggle is a **server-side swap of the two saved looks**, broadcast to everyone who sees the player. The client already renders
whatever form the look names, so it needs no new rendering path.

```
spell cast (Visage / Two Forms)
  -> Player::ToggleRetailForm()
       swap active <-> parked (memory)
       write both rows in one transaction
       SMSG_ESTERIA_RETAIL_LOOK (version 2, new form) to self + everyone in range
  -> clients: ApplyLook sets the form, SyncBodyModel swaps the body
```

- **No transform aura.** The spell's own transform effect is cancelled, so there is no human display, no costume NPC and nothing to
  cancel.
- **Persistence for free.** Login, character select and visibility sends all read the active row, so the last form is kept.
- **Characters without a saved pair** (created before Phase 3) cannot toggle. Visage then keeps its old behaviour; Two Forms does
  nothing. They gain a pair the next time the creator commits one (a customize).
- **Order of forms:** Wolf (1) / Human (2) for Worgen, Dragon (3) / Visage (4) for Dracthyr. A pair must be exactly those two.

## 3. Server changes (EsteriaWoW)

| Piece | Where |
|---|---|
| Load the parked look at login | `CHAR_SEL_FORM_LOOK`, `PLAYER_LOGIN_QUERY_LOAD_FORM_LOOK`, `Player::LoadFormLook` |
| `Player::ToggleRetailForm()` | swap, persist, broadcast; false when there is nothing to swap |
| Dracthyr | `spell_race_visage` calls it first; the old Chosen-Identity transform stays as the fallback |
| Worgen | new `spell_race_two_forms`, bound to 68994/68995/68996/110020, cancels the transform and calls it |
| Script binding | `u_custom_server_2026_10_24_00_two_forms.sql` (`spell_script_names`) in the module's `db-world/updates/` |

Rules kept deliberately small: the existing spell cooldowns apply (Visage 5 s, Two Forms the global cooldown). No combat, mount or
vehicle restriction yet.

## 4. Client changes (wowdecomp), not started

1. The creator starts a Worgen or Dracthyr on the native form (Wolf, Dragon) when it has nothing remembered, as retail data has it.
2. Treat "no explicit form" for a Dracthyr as Dragon where the code checks for Dragon (bare body, wings).
3. Refresh a character-select entry when its look arrives after creation (it still shows the pre-commit default until the next list).

## 5. Test plan (after the server image is rebuilt)

| # | Do | Expect |
|---|---|---|
| 1 | Log in the Dracthyr (Visage active), cast Visage | body swaps to the Dragon with its saved look |
| 2 | Cast it again | back to Visage |
| 3 | Log out in Dragon form, log in | Dragon; character select shows Dragon |
| 4 | Second client in range | sees each swap |
| 5 | Same four for the Worgen with Two Forms | Wolf and Human |
| 6 | Cast on a character with no saved pair | no error; Visage as before, Two Forms nothing |
| 7 | Database after a toggle | `character_retail_look` and `character_form_look` have swapped forms |

## 6. Open questions

- Should combat, mounting or flying block the toggle? (Retail Dracthyr switch freely; Worgen Darkflight forces Wolf.)
- Darkflight (68992): should it force the native form like retail? Easy to add once the toggle is in.
- Chosen Identity (110687) picks which race the Visage wears. With saved forms it has no effect on rendering; leave it, remove it, or map it to a Visage option later?
- A transition effect on the swap (Cata plays anim kits 486/487 and 496/497): later.

## 7. Server status

Written in `EsteriaWoW`, uncommitted. A Docker image build (`esteria-twoforms:20261024b`, no deploy) is the compile check. Not run.

| Change | File |
|---|---|
| `CHAR_SEL_FORM_LOOK` | `CharacterDatabase.h/.cpp` |
| Login query `PLAYER_LOGIN_QUERY_LOAD_FORM_LOOK`, `LoadFormLook` | `Player.h`, `CharacterHandler.cpp`, `PlayerStorage.cpp`, `Player.cpp` |
| `Player::ToggleRetailForm()`: validates the pair (right race, sex, forms 1/2 or 3/4), swaps, writes both rows in one transaction, sends `SMSG_ESTERIA_RETAIL_LOOK` to the player and everyone in range | `Player.cpp` |
| `spell_race_visage` tries the toggle first; the old transform is the fallback | `mod-custom-server/src/race_racials.cpp` |
| `spell_race_two_forms` cancels the spells' transform and toggles | same file, registered in `AddSC_race_racials` |
| Script names for 68994/68995/68996/110020 | `mod-custom-server/data/sql/db-world/updates/u_custom_server_2026_10_24_00_two_forms.sql` |

The SQL is **not applied**: `ac-db-import` fails on an older update, so it is applied by hand when the image is swapped in, like the
form-look table. Until then the running server keeps the old Worgen behaviour.
