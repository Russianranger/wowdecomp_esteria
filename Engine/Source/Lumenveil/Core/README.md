# Lumenveil core (ported)

These files come from [Lumenveil](https://github.com/lumenveil/Lumenveil) `engine/src` at commit
`8e9a74ee2001390ec413097fa88ecff13832f391` (release 0.1.1), a modified version of
[CoAVolFog](https://github.com/jealous-sound/coa-vfog). They are under the GNU General Public License version 2
(`LICENSE` here). A build of this client that includes them is a work based on them: whoever receives that binary
must be able to get this source under the same licence.

## Kept as upstream wrote them

`cloud_noise`, `config`, `fog_data`, `fog_model`, `log`, `region_settings`, `region_zones`, `regions`, `spots`,
`world_controls` (`.h` and `.cpp`), `relief_classes.h`, `client_world.h`, `belt_lantern`, `corpse_lights`, `lamp_catalog`, `lamp_selection`, `lamp_data.h`,
`effect_lights.h` (2026-10-28), and `lamp_tiles` with `<d3d9.h>` swapped for `<d3d9types.h>`.

## Changed for wowdecomp (2026-10-28)

- `engine.h`: rewritten. The stock client's addresses and the Direct3D 9 device are gone; `FrameInputs` and the
  declarations the files above use keep their names. The frame inputs are read from the client's source in
  `WoW/Source/Ui/LumenveilInputs.cpp`, the opaque state in `../Lumenveil.cpp`.
- `hooks.h`: rewritten. No call-site patches (the client calls `../Lumenveil.h`), and no far clip lift, since this
  client's `World::ValidateFarClip` already lets `farclip` reach `World::kMaxFarClip`.
- `client_world.h` is implemented in `WoW/Source/Ui/LumenveilInputs.cpp`: the clock and weather are read from the
  client; setting them (the region panel's overrides) is not ported yet.

The rest of the mod (the renderer, its HLSL passes, the water, lamps, relief, the setup tool and the settings
windows) is not here yet; it comes over in later steps, as GLSL passes over `Engine/Source/Gx/GxEffects.h`.
