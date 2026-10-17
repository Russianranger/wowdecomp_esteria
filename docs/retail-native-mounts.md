# Native Retail mounts

Mastercraft Gravewing is the first additive mount pack. Original Blizzard files are
acquired with CascLib from the same pinned Retail build as the Arakkoa sword pack.
The installed asset store contains original decoded CASC bytes. The engine decodes
those files into runtime memory; no converted M2, SKIN, animation or BLP is emitted.

## Identity

| Namespace | Retail | Esteria |
| --- | --- | --- |
| Mount | 803 | Collection through summon spell |
| Learning item | 186479 | 1000001 |
| Item display | — | 160001 |
| Summon spell | 215545 | 300000 |
| Creature | — | 4500000 |
| Creature display | 100108 | 200000 |
| Creature model | 12614 | 200000 |
| Spell icon | Original icon FileDataID 3852563 | 600000 |
| SkillLineAbility | — | 400000, skill line 777 |

The resolved recipe is `config/mastercraft-gravewing.json`. Build-matched Mount,
MountXDisplay, CreatureDisplayInfo and CreatureModelData records identify model
3852506 and brown body/saddle textures 3852567/3852551. The original icon is also
downloaded. Eleven original files include normal and Retail LOD skins, fixed textures
and the selected replaceable textures. Only normal advertised profiles are opened
by this engine; unused LOD originals remain preserved.

## Integration

`native_mount.py` reserves IDs against client providers, server DBCs, SQL overlays
and persistent SQLite allocations. It clones the running server's flying-carpet
spell 61309, adds a new mounted creature/display/model and mount collection row,
and creates a consumed learning item. Target gameplay is +100% ground / +280% flight,
with a level-one learning item for testing and no Retail covenant restriction.

The client merges seven supplemental tables through its normal database loader.
The server receives four uniquely named DBC continuations plus guarded additive
item/creature SQL. Base archives and base server DBCs are preserved. SpellIcon is
client metadata and has no corresponding server store.

D3D11 handles the source's large bone palette with a larger constant buffer. The
native material path supports its third saddle glow texture, preserving stock
reflection, lighting, fog and shadow permutations. Constant camera FOV tracks and
single-texture ribbon tracks are decoded in memory. Shared SKELs, chunked external
animations, animated camera FOV and Retail particle layouts remain unsupported.

## Validation

`RetailAssetTests` checks bounded parsing and additive mount table merges, including
preservation of existing Esteria spell IDs above thirteen million. `RetailShaderProbe`
extends and compiles all sixteen stock reflection permutations through the actual
D3D11 shader translator. `check_server_install.py` exercises installation, rerun and
collision rollback in disposable MySQL tables. `verify_install.py` verifies original
asset hashes, SQL definitions, every server continuation and all seven mounted tables.

The installed pack's `receipt.json` records the current evidence state. Learning,
preview rendering and actual mounted-world rendering are distinct checks. Live
acceptance also includes rider fit, movement, flight, dismounting and persistence.

On 2026-10-17, the D3D11 world test showed the full summoned mount beneath Kekelol
in Valley of Trials, with its saddle, wings and rider attachment visible. The current
D3D11 log compiled `Retail_Opaque_Reflection_Glow:4` successfully. Learning and
collection registration were also confirmed. Movement, flight and persistence still
need separate acceptance; the login overlay alone was never evidence for those.
The subsequent world test exposed intermittent stretched scene geometry even while
dismounted, also visible in the minimap. Full visual acceptance remains pending that
renderer defect. `WOW_D3D11_DEBUG=1` records driver validation warnings and errors in
the current `Logs/d3d11.log` for diagnosis.

The normal launcher is `LaunchRetailNative.cmd` beside the client executable. It
clears diagnostic preview variables and passes `-d3d11` explicitly. Without that
argument, the target defaults to D3D9, whose 256 vertex constants cannot hold this
mount's 104-bone section palette. That mismatch leaves the rider and ribbons visible
while the body fails to render. The isolated preview already selected D3D11, which
explains why it could render while the normal world launch could not. Renderer
diagnosis must use the current launch's `gx.log`, not a stale `d3d11.log`.
A screen preview is diagnostic and does not
substitute for a server mount. Existing learned items/spells must be considered
before removing a pack; IDs are never recycled automatically.

## The Writhing Brood (Midnight 12.1)

`config/writhing-brood.json` (Retail mount 3058, display 145412, model 7551444) is the first
ground mount and the first model using chunked `.anim` files. Additions it required:

- Chunked animation files: an `AFM2` chunk wraps the legacy `.anim` payload; the 8-byte header
  is dropped when the file is opened (originals stay untouched on disk). Offsets in the model
  are relative to the payload.
- Particle emitters use a newer layout and are **omitted** (count cleared); mesh, skinning,
  ribbons and animation are unaffected.
- Explicit shader 0x15 (`Diffuse_EdgeFade_T1_T2` / `Combiners_Mod_Mod`) maps to
  `Diffuse_T1_T2` / `Combiners_Mod_Mod` without the edge fade (no EdgeFade vertex shader in 3.3.5).
- The batch geoset index is not used by the legacy renderer and is no longer range-checked.
- Fly/Swim animation swap and the plain mount spell visual (rider seated) apply as for the Gravewing.
- The mount is a ground mount: its donor spell is a +100% ground mount, and the creature
  display scale is 1.6 as in Retail.
