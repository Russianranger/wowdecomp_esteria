# Indexed MADD parameter storage

This describes a structural inference checked against the cowl MADD material and
the RequiredAnims bank's MADD material in the supplied `HOTS_M3` directory.
The storage decoder itself does not identify shader roles or node operations.
A separate named-input adapter is described below. The identifier hashing
algorithm and animation ownership of MADD fields remain unconfirmed.

The existing schema calls the MADD references `unknown2` (U32 identifiers),
`unknown3` (U32, retained but not interpreted) and `unknown4` (binary CHAR).
In the supplied buffers, `unknown2` exactly matches the root identifier table
in `unknown4`, including order. The new decoder cross-checks that invariant.

All integer storage is little endian. Offsets are absolute byte offsets within
`unknown4`, including when values are unaligned.

| Root byte offset | Storage | Observed interpretation |
| --- | --- | --- |
| 0 | u32 | Group count |
| 4 | u64 | Offset of count u32 group IDs |
| 12 | u64 | Offset of count u64 group block offsets |

Each group block has the following header. The group end is the next larger
group offset, or the payload end for the final group. Root tables precede the
first group. Neither original ordering nor four-byte alignment is assumed.

| Group byte offset | Storage | Observed interpretation |
| --- | --- | --- |
| 0 | u32 | Field count |
| 4 | u64 | Offset of count u32 field IDs |
| 12 | u64 | Offset of count u16 field byte widths |
| 20 | u64 | Offset of count u8 field flags |
| 28 | u64 | Offset of count u64 field value offsets |

Each value occupies its declared byte width. The source sample has widths 4,
8, 20 and 32. Treating these widths as shader type enums would be unsupported.
Empty groups have count zero and four zero offsets. The decoder rejects
out-of-block/overflowed offsets, overlapping tables/values, aliased groups,
duplicate identifiers and excessive counts. Future layouts rejected by this
structural decoder remain available as original opaque bytes with an explicit
`bufferDecodeError`; they do not acquire guessed diffuse bindings.

The core owns every decoded value. The parser is its own `BufferMaterial.cpp`
object inside ASECM3Core and does not depend on game state, Python or generated
runtime files. `ASECM3MaterialInspect` exposes groups, fields and original offsets.
The viewer's native asset info exposes counts and structural decode errors.
Complete MADD shader evaluation remains unfinished. Recognized named inputs now
use the preview adapter below; unsupported inputs retain diagnostic geometry.

Evidence under `WoW335_ASEC/verification/m3`:

- `buffer-material-parity.json`: 22 files, two MADD materials, 25 groups,
  58 fields, comparing IDs/widths/flags/offsets/value bytes to payload bytes
  supplied by the separate source M3 parser.
- `buffer-portable-material-parity.json`: the same source comparison with the
  standalone module build.
- Both parity reports also check four corrupted real-file buffers: unknown or
  invalid indexed storage reports an error while retaining the original bytes.
- `buffer-fixtures.json` and `buffer-portable-fixtures.json`: 24 analytical
  and malformed-input fixtures in each build.
- `material-buffer-regression.json`: existing full material byte/reference
  checks on 22 files and five malformed material cases.
- `scene-buffer-regression.json`: 822 source scene cases across 22 files.
- `runtime-buffer-probe.json/log`: native Glue cowl counts (13 groups/29 fields)
  and existing surface/bank/action/composite/view/state regression checks.

These checks validate storage decoding, not rendered material fidelity.

## Named-input surface adapter

Read-only extraction of the installed HotS build 98348 DX11 shader cache supplied
named DDBS shader blocks and DDPB parameter records linked through DDST strings.
All four quality sections agree on the names/types for the source material's
serialized fields. The extraction script validates the containing section sizes
and references, then cross-checks the source field IDs. Earlier exploratory
offset guesses are not used by the final parser. The final metadata record
layout is 40 bytes: name offset (u64), two reserved u64s, ID (u32), default-value
offset (u32), kind/flags (u32), reserved u32.

Source cache SHA256:
`0d8e3756051c3399ae4f9c8461450bc736f2e0feae261437ffafc3d75a362143`.
The full cache stays in local verification/research. Only a small table of IDs,
names and kinds is copied into data/madd_shader_inputs.json and embedded by
generate_buffer_inputs.py. CMake checks its hash. Runtime needs neither HotS nor
its shader cache, Python, additional shader files, or a disk M3-to-M2 conversion.

BufferSurface.cpp is a separate core object. It retains the original material
and builds a separate immutable surface for the existing native preview shader.
The following routes use the actual named IDs; no texture filename or list-order
role heuristic is used:

| Block | Named inputs used | Preview route |
| --- | --- | --- |
| Normal | TexNormal, NormalUVSelection, NormalUVTransform | BC3N normal |
| DiffuseTeamColor | TexDiffuse, DiffuseUVSelection/Transform/ChannelSelection | Diffuse and team mask |
| Gloss | TexGloss, GlossUVSelection/Transform/ChannelSelection | Gloss |
| Specular | TexSpecular, SpecularUVSelection/Transform/ChannelSelection | Specular |
| LightingDeferred | Specularity | Preview specular exponent |
| EmissiveFast | TexEmissive, EmissiveUVSelection/Transform/ChannelSelection, EmissiveControl | Emissive |
| EnvioMask | TexEnvioMask, EnvioMaskUVSelection/Transform/ChannelSelection | Environment mask |
| EnvioBlurred | TexEnvironmentMap, EnvironmentMapUVWTransform/ChannelSelection, EnvioControl | Cube environment |
| TwoSided | Presence of block | Disable culling |

The eight-byte texture bindings contain a source texture-path index and flags;
the supplied repeat-U/V flag value 3 is supported. Other sampler values report a
limitation. UV selection 0..3 maps to the corresponding source UV set; normal
maps currently require UV0's tangent basis. Only the observed identity 32-byte
UV transform is supported until its general transform semantics are established.

Channel controls use the source-observed RGBA/RGB/R/G/B/A routes and finite
multiply/add/brightness values. The invert flag at byte 16 must be zero;
clamp at byte 17 supports both zero and one and is applied after gains and
before subsequent shader composition. The last two bytes remain reserved. The
GPU fixture uses AO modulation so a missing clamp is visible. The preview
routing of channels/control enums follows source MAT_/LAYR equivalents;
names/types are direct shader-cache evidence, while exact output pixel
agreement still requires source-engine comparison. Inversion, unknown modes,
malformed indices, unknown fields, unsupported UVs and dynamic-parameter
payloads remain explicit limitations.

ECSpec, MaterialFinalize, Fog and FOW are recognized blocks, but their source
lighting/finalization/world effects are not reproduced by this preview. Source
HDR fields are not yet interpreted; the adapter's HDR coefficients are explicitly
one. This is the existing preview lighting response with named MADD inputs,
not a claim of complete source shader graphs, energy conservation, tone mapping,
world lighting, animation or material-class support.

Additional evidence: buffer-surface-bindings.json and
buffer-surface-portable-bindings.json cover both MADD materials/14 layers,
12 unsupported or malformed input cases and six channel/gain routes.
material-buffer-surface-regression.json and scene-buffer-surface-regression.json
retain material and 822-case source scene checks. Native Glue evidence in
runtime-buffer-surface-probe.json/log records four cowl shader submissions,
including normal, specular and cube inputs, with zero state failures.
buffer-surface-gpu-regression.json records 60 shared-shader hardware fixtures.
Windows capture failed twice; source-model appearance remains unverified.
