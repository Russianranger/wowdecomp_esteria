# Esteria Mount Studio

Rust/Tauri desktop tool for replacing existing stock mount appearances with the
original assets from an installed Retail client or a pinned Retail CDN build. Uses wowdecomp's native Retail
reader and existing AdvFly controller. **No retroporting, resampling, animation
remapping, or model/texture conversion.**

The app scans the client read-only, protects custom/native additions, resolves
Retail mount/display variants, downloads original dependencies with persistent
checkpoints, validates them with the client's native loader in offline mode, and
exports compressed MPQs with SHA-256 readback receipts. It never installs a
package, changes a source MPQ, edits a database, or restarts the game/server.

## Requirements

- Windows 10/11 x64 and your target 3.3.5 client.
- An installed Retail `wow` product for Local mode, or internet access for CDN mode.
- The installer includes embedded Python, StormLib, the native probe, DB2 reader
  source, four build-aware metadata schemas, default `RetailCasc.ini`, Visual C++
  runtime DLLs, mount display mappings, and an offline WebView2 installer.
  Python, Docker, Node, Rust and the development checkout are not runtime requirements.
- Docker is optional: choose Live world database mappings only when you want to
  use a running Esteria database instead of the shipped snapshot. Credentials stay
  inside Docker and the application's database access is read-only.

Installed Retail is the default source. Select the WoW directory containing
`.build.info` and `Data`, or its `_retail_` child; the app finds the active `wow`
build. Classic and Forever products are rejected. Local mode reports missing
files instead of silently downloading them. Switching between Local and CDN is
allowed within a project only when build and language match exactly.
Progress distinguishes local Retail reads, CDN/cache access and project-cache
reuse. The standalone engine's topmost busy notice is disabled in GUI workers;
Mount Studio owns its progress display. An "acquiring" count describes asset
acquisition and does not establish that bytes came from a network transfer.

The Settings page contains source selection, Retail location, CDN build/region,
language, output location, automatic folder opening, timeout, archive size limit,
theme, animations, default mount filter and mapping source. Folder fields have
native Browse dialogs. Check setup tests the selected source and bundled components.

On this workstation the existing `G:\3.3.5a - Dev` target is detected. New projects
default to the application's local-data directory; existing saved paths are retained.
MPQs default to `exports/` in the project folder. A custom output folder must stay
outside the target client and Retail installation.
Source bytes, work state, schemas and generated packages stay outside Git.

## Build and launch

The commands below are manual instructions. Repository policy requires explicit
authorization before an agent configures or builds. Existing configured build
directories can be reused; no whole-server build is required.

From the wowdecomp root, rebuild the changed helper, tests, and client using the
existing Retail-enabled configuration:

```powershell
cmake --build build/retail-assets --config Release --target RetailAssetProbe RetailAssetTests WowClient --parallel
build/retail-assets/Release/RetailAssetTests.exe
build/retail-assets/Release/RetailAssetProbe.exe --capabilities
```

Use the existing build directory actually configured for your client if different.
Deploying the rebuilt client is a separate action; retain your existing client
backup/verification workflow. The MPQs require the packed-reader and mount-override
changes in this checkout; older installed clients do not understand this package.

Developer builds require Python, Node, Rust and Visual Studio C++ tools. From `Tools/MountStudio`:

```powershell
npm ci
npm run dev
# Windows installer and executable:
npm run build
```

`prepare_release.py` runs before development/release builds. It stages and checks
the embedded runtime, original parser source, native binaries, redistributables,
schemas, default config, mapping snapshot, licenses and a SHA-256 file manifest.
Its development-only input paths can be supplied with `--probe`, `--storm`,
`--reader` and `--definitions`. These paths are not used by the installed app.
The final runtime lives under `runtime/`, relative to the executable. Worker
processes always use the bundled interpreter and tools, regardless of host PATH.

## Workflow

1. Open Settings and select Local Retail (default) or CDN, then set paths.
   A work directory is bound to one client and one Retail build/locale.
2. **Scan client** reads the effective Spell, CreatureDisplayInfo and
   CreatureModelData providers, including custom patch suffixes and configured
   supplemental native tables. Mounted creature entries resolve through the world's
   `creature_template_model` mapping snapshot, custom JSON, or optional live database.
   Shared display IDs form one selectable appearance;
   its details retain every summon, speed and placeholder spell variant. Distinct
   display/skin appearances remain separate, even when their names match.
   Stock mount eligibility comes from the stock
   3.3.5 Spell table, not a guessed upper ID limit.
3. **Fetch retail catalog** reads Mount, MountXDisplay, CreatureDisplayInfo and
   CreatureModelData from that same source build. Match by summon spell, preferring
   an exact display variant. Multiple variants stay unselected for manual review.
   Unavailable/encrypted source records remain unresolved; they are not fabricated.
4. Filter **Flying**, **Ground**, **Water**, or **All mount types**, independently of
   status. Types come from the effective summon spells' flight/swim auras; an
   appearance with different movement variants can appear in multiple filters.
   Flying is the default filter and the default selection for newly matched mounts.
   **Select only filtered** replaces the download selection with matched mounts in
   the current search/type/status filters. **Clear filtered** clears only those rows.
   Review and select replacements. Custom spells and custom/native model paths
   are protected. An unmatched stock mount can be assigned an explicitly reviewed
   appearance from the full source catalog. **Save choices** persists selections.
5. **Acquire / resume** follows SFID skins (including LODs), TXID textures, SKID
   skeletons, SKPD parents, all AFID animation variations, and referenced bone,
   physics and geometry files. No animation is stripped. Downloads are individually
   hashed and checkpointed; damaged cache outputs are reacquired.
6. Offline validation invokes the real native model reader using only acquired
   originals. AdvFly sequences are inventoried separately from ordinary sequences;
   a mount without authored AdvFly does not acquire invented animation data.
7. **Export MPQ** shows how many selected mounts are ready. If acquisition was paused
   or some mounts failed, choose **Export completed mounts** to package only validated
   selections, or **Resume downloads** to continue. Unfinished mounts remain selected
   and cached; the export receipt lists every skipped mount and its status. The
   worker's strict export path still requires every selected appearance to be validated.
   Shared
   display conflicts and displays used by protected custom mounts block export.
   Packages are split conservatively below the classic MPQ size boundary, compressed,
   read back and hash checked. Source table providers are checked again before
   export; changed client data requires a new scan. `receipt.json` records every entry.

Pause is cooperative between files and archives. An in-flight CDN operation can
take up to its timeout to return. On a process crash, inspect the PID in `job.lock`;
remove that lock only after confirming no worker is running. Finished assets survive.
Reopen the same work directory and use **Open project** to resume.
Projects from v0.1.0 are backed up and rescanned on opening to resolve real display
IDs and group spell variants. Downloads remain cached; compatible saved choices
are retained. Conflicting choices on a shared appearance require review.

Dark mode is the default. The header's Light mode / Dark mode button saves the
chosen theme. Controls, expandable details and progress indicators animate;
Windows/browser reduced-motion preferences disable those animations. Worker error
messages stay visible after the job stops, rather than being replaced by an exit code.
After a verified export, Explorer opens the exact package folder by default.
Disable this in Settings if preferred; Open exported MPQ folder remains available.

## Package contract

```
EsteriaMounts/mounts.csv
EsteriaRetail/wow-<build-config-key>/<decimal-locale-mask>/<FileDataID>.bin
```

`mounts.csv` contains target display, source product/build/locale, model FDID,
three replaceable texture FDIDs, and source display scale. The Unit mount creation
path uses local copies of the display/model records, so ordinary NPCs sharing the
original display are unaffected. Existing spell IDs, learning items, collection
state, speeds, sounds, collision policy and server behavior are retained.

SFile registers a low-level MPQ original reader with CascSource. It bypasses
Retail dispatch to avoid recursive source locks, reads packed originals before
CASC/loose cache, and preserves native in-memory preparation. Build and locale
namespaces cannot cross-route. A damaged packed read fails instead of fetching a
replacement silently. Unrelated Retail assets continue using their existing source.

Exported packages are staged under `work/exports/<unique-id>/`. Copy the complete
MPQ set into Data only after backing up and updating the client binary. Keep one
MountStudio export installed at a time: the highest-priority `mounts.csv` defines
the complete selected replacement set. Remove those MPQs to roll back appearances;
base archives and saved mounts were never modified.

## Verification and remaining boundaries

```powershell
python -m unittest discover -s tests -v
# Include a real StormLib compression/readback test:
$env:STUDIO_STORMLIB = 'C:/Tools/StormLib.dll'
python -m unittest discover -s tests -v
node --check ui/app.js
cargo fmt --manifest-path src-tauri/Cargo.toml --check
```

Tests cover mount matching, custom protection, archive order, malformed chunks,
animation variation/parent closure, interrupted/corrupt cache recovery, build
isolation, pause, and actual compressed MPQ readback. Native test source also
covers packed original routing and strict replacement catalog parsing.

`tests/smoke_release.py` relocates the shipped runtime into a directory with spaces,
removes developer tools from PATH, and exercises setup, client scan, cached source
metadata, native model validation and compressed MPQ export with embedded Python.
It uses a saved project and its original asset cache as inputs; no game assets
are included in the software package. Live extraction from a newly installed
Retail client remains a separate test.

`tests/preview_ui.py` can render a saved real inventory in headless Chrome, capture
a screenshot and check search, details and disabled custom selections. Its IPC
adapter exists only in the disposable browser test page, not the shipped app:

```powershell
python tests/preview_ui.py --project G:/MountStudioWork/project.json --chrome 'C:/Program Files/Google/Chrome/Application/chrome.exe' --output G:/MountStudioWork/ui-preview.png
```

Offline validation does not prove mounted-world rendering, rider fit, effects,
AdvFly transitions, relog persistence or performance. Test representative ground,
flying, shared-skeleton and multi-pass mounts in-game on the exact installed package.
Collection/NPC model previews use their existing paths; this version replaces the
mounted-world appearance. Unsupported source shader/material/appearance layouts
remain explicit failed jobs rather than lossy conversions.
