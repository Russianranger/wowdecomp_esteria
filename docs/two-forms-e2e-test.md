# Two Forms: end-to-end test plan (client steps 2 and 3 against the step 1 server)

**State:** plan only. Nothing below has been run. Facts were read from `EsteriaWoW` and the local Docker host on 2026-10-24.

## What the host looks like

| Fact | Evidence |
|---|---|
| The realm is `ac-worldserver` running `esteria-newoptions:20261023`, up since 00:40, with 518 characters in `acore_characters` | `docker ps`, `docker inspect`, `SELECT count(*)` |
| Worldserver images are tagged per feature and an older tag is kept for rollback (`pre-newoptions`, `rollback-20261021`, ...) | `docker images` |
| They are built through compose: the image carries the `esteriawow` compose labels, and the tag comes from `docker-compose.override.yml` | `docker image inspect` |
| `ac-db-import` exits 1 (`ERROR 1060 Duplicate column name 'extraAppearance'` from an older update), so `ac-worldserver` cannot be started through its `depends_on` | `docker logs ac-db-import` |
| The worldserver image sets `AC_UPDATES_ENABLE_DATABASES=0`: it never applies SQL itself | `docker history` |
| `character_retail_look` already exists, `character_form_look` does not | `SHOW TABLES` |
| Compiling the server locally is not possible (no Boost.Asio, no MySQL headers), so the Docker build is the first real compile of the step 1 code | earlier compile attempt |

Consequences: SQL is applied by hand, the worldserver is started with `--no-deps`, and the new image gets its **own tag** so
the running one is never overwritten.

## A. Back up (no change to the running realm)

```bash
cd /r/Users/Zach/Documents/GitHub/EsteriaWoW
docker tag esteria-newoptions:20261023 esteria-newoptions:pre-twoforms
mkdir -p "/g/3.3.5a - Backups/twoforms-pre-test"
docker exec ac-database mysqldump -uroot -ppassword --single-transaction acore_characters > "/g/3.3.5a - Backups/twoforms-pre-test/acore_characters.sql"
```

The first line adds a rollback tag to the image that is running now. The dump is of the characters database only.

## B. Apply the SQL (adds one empty table; nothing existing changes)

```bash
docker exec -i ac-database mysql -uroot -ppassword acore_characters < data/sql/updates/pending_db_characters/rev_20261023233000000_character_form_look.sql
docker exec ac-database mysql -uroot -ppassword -e "SHOW CREATE TABLE acore_characters.character_form_look\G"
```

`CREATE TABLE IF NOT EXISTS`, so a rerun is harmless. It is applied by hand because `ac-db-import` fails on an older update.
The new worldserver prepares statements against this table at startup, so this step must come before the swap.

## C. Build the image under a new tag

A new compose file (untracked, nothing else edited) gives the worldserver a new image name, so `build` does not retag the
running one:

```yaml
# docker-compose.twoforms.yml
services:
  ac-worldserver:
    image: esteria-twoforms:20261024
```

```bash
docker compose -f docker-compose.yml -f docker-compose.override.yml -f docker-compose.twoforms.yml build ac-worldserver
```

- This is the first compile of the step 1 server code. The build context is the working tree, so it also contains every other
  uncommitted change in `EsteriaWoW` (the same as your earlier images).
- `Opcodes.h` changed, so most of the game library recompiles: expect a long build, faster afterwards (ccache is a cache mount).
- A compile error stops the build with nothing deployed. I fix it and run the same command again.
- The realm is untouched until step D.

## D. Swap the worldserver (this disconnects everyone; ask first)

```bash
docker compose -f docker-compose.yml -f docker-compose.override.yml -f docker-compose.twoforms.yml up -d --no-deps ac-worldserver
docker logs --tail 80 ac-worldserver
```

Look for a clean start to "ready", no SQL or prepared-statement errors, and no `CMSG_ESTERIA_FORM_LOOK` warnings yet.
`ac-chat-forward` shares the worldserver's network namespace and will need `docker start ac-chat-forward` afterwards if you use it.

**Rollback** (about a minute, and the image is already there):

```bash
docker compose -f docker-compose.yml -f docker-compose.override.yml up -d --no-deps ac-worldserver
```

The extra table can stay; the old server ignores it.

## E. Test cases

Needs: the dev client (`G:\3.3.5a - Dev`, new `Wow.exe`), and for T8 a second client on a second account that you create.
I do not create accounts or handle passwords.

| # | You do | Pass if |
|---|---|---|
| T1 | Create a **Dracthyr**: edit Dragon and Visage, press Create | `Logs\RetailRace.log` shows `form commit sent`, then `form commit acknowledged ... result 0` within a few seconds |
| T2 | Create a **Worgen** the same way (Wolf and Human) | the same two log lines |
| T3 | I query the database for both characters | `character_retail_look` and `character_form_look` each hold one 132-byte row: version 2, race and sex right, the two forms different |
| T4 | Open character select | the Dracthyr shows the form that was active at creation, with its choices; the Worgen likewise |
| T5 | Enter the world, log out, log back in | same look, same form |
| T6 | Create a Dracthyr while **Dragon** is the active form | character select and the world show a Dragon (bare), not Visage |
| T7 | Create a **Human** and a **Dracthyr from before this change** | the Human behaves as before; the old Dracthyr loads with no errors and its default form |
| T8 | A second client in range of the Dracthyr | it sees the same model and look (the Dragon may show one frame of Visage first) |
| T9 | Send nothing: use an old `Wow.exe` from `_backups` to create a Dracthyr | creation works and nothing is written to `character_form_look` |
| T10 | Delete a test character | its rows leave both tables |

Database queries I run for T3 and T10 (read-only), with a small script to decode the blobs:

```bash
docker exec ac-database mysql -uroot -ppassword -N -e "SELECT c.guid, c.name, c.race, c.gender, HEX(r.data), HEX(f.data) FROM acore_characters.characters c LEFT JOIN acore_characters.character_retail_look r ON r.guid=c.guid LEFT JOIN acore_characters.character_form_look f ON f.guid=c.guid WHERE c.name IN ('<test names>')"
```

Decoding (byte 0 version, 1 race, 2 sex | form << 4, 3 option count, then 64 little-endian u16): version 2 with a form in the
high nibble of byte 2 for a form look; version 1 only for a character created by an old client.

## F. What a failure looks like

| Symptom | Likely cause |
|---|---|
| Worldserver exits at start with a SQL error | step B was skipped |
| `refused with result 1/2/3` in the worldserver log | ownership, bad look or wrong race; the log line names the account |
| `form commit dropped: no acknowledgement` in the client log | the server did not receive or answer 0x525; check the swap and the log above |
| `form commit dropped: <name> not in the character list` | create succeeded but the name, race or sex did not match the list entry |
| Character select shows Visage for a Dragon | the version-2 look was not delivered in the enum tail; check `RLE1` handling |

## G. Clean up

Delete the test characters in the game, keep the dump until you are satisfied, and delete `docker-compose.twoforms.yml` (or keep it
and move the tag into your override) once the new image is accepted.
