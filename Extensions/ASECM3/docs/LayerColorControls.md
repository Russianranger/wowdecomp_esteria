# Layer color-control coverage

## Source clamp flag

LAYR.flags bit0x20 is named color_clamp in the module schema and clampColor in
the older independent m3addon schema. The m3studio layer UI exposes Clamp.
The portable LayerClampsColor function now combines this source flag with the
existing normalized MADD adapter marker, asec_color_clamp. Source flags must
fit an unsigned32bit integer; nonfinite/invalid controls report an error.

GxSurface uses this function when filling the existing layer gain.w constant.
The already implemented FinishLayer shader operation clamps the layer result
after channel selection/multiply/add/RGB brightness and before layer composition
or material-level HDR factors. No shader bytecode, sampler allocation or
register-bank layout changed. This connects source data to the existing adapter
operation; it is not a new proof of exact HotS operator ordering or lighting.

The fixed-function diagnostic fallback remains an approximation and is not
claimed to implement the full layer pipeline. Layer inversion is also still
unimplemented; the supplied active-layer audit found no bit0x10 users, and no
ordering was guessed for that feature.

## Current corpus and evidence

The22supplied M3/M3Afiles decode49materials of types1(Standard),3(Composite)
and12(MADD). All active bitmap/constant layer roles are among the shader's
implemented roles. This is a role inventory, not full material fidelity.
197active source layers set bit0x20, which was previously ignored by the
standard-layer adapter. The full layer-control comparison covers788records,
including inactive layers and generated MADD surface layers;209enable clamp.
It compares native policy output to independent interpretation of decoded source
flags and adapter markers for all22files, with no mismatches.

Evidence in verification/m3:

- material-current-corpus-audit.json: decoded material/type/active-role inventory.
- source-layer-clamp-parity.json:788record source-policy comparison.
- source-layer-clamp-gpu.json: existing170case D3D9 HAL fixture passed, maximum
  byte error1against fixture expectations, including the pre-existing layer
  clamp-before-AO case. It is not source-model visual parity or a test of the
  actual client C++ constant upload.
- source-layer-clamp-build.log: core/client-library/inspector builds.

The GPU fixture initially failed device creation in the restricted session;
the same executable succeeded on the interactive desktop. It renders and reads
only its own hidden fixture render target. No user-screen capture is involved.

This source/library correction has not yet been linked into the installed client.
The current installed CD258138 build and sourcekitv5remain the prior matching
runtime snapshot. Full model appearance and unchanged native M2 rendering must
be checked when this correction is included in a subsequent client update.
