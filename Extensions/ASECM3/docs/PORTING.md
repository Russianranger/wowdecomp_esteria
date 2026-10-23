# Building and transferring ASECM3

The module is in progress. A successful core build does not establish a working
port of the native renderer, physics behavior, effects or host integration.

## Components

| Component | Host requirement |
| --- | --- |
| ASECM3Core.lib | C++17, vendored Bullet, generated metadata/data shipped here; no client or Python runtime |
| ASECM3Bullet.lib | Vendored collision/dynamics/math implementation; preserve its license |
| ASECM3Client.lib | Matching native Gx/Model2/Tempest/SFile/Unit headers and diagnostics; Windows D3D9/9Ex |
| ASECM3Disabled.lib | Disabled host-call boundary; no source parser, M3 renderer or physics instance |
| ASECAssetViewer | Optional native preview frame and its FrameXML/Lua integration |

Compiled native libraries use the current x64 MSVC Release /MD host ABI. Rebuild
from source when the compiler, runtime, architecture or host headers differ.
They are static libraries, not a DLL that can be injected into a stock client.
No original M2 data layouts or shared placeholder geometry are extended.

The world-effect Lua implementation is module-owned. Call
ASECM3::Client::RegisterWorldEffectScriptFunctions() from the host's existing
Player/script registration bootstrap, as shown in host-hook-reference.json.
The current host adds one call to ASECPlayer_Register. Preserve adjacent
model.m3.effectbank and declared M3A companions when transferring client model
data; these optional source dependencies are separate from the module/runtime
asset export. See WorldParticles.md for the current world pass and evidence
limits, and ParticlePreview.md for the preview baseline. A pre-login probe pass
does not establish real world queue placement, appearance or gameplay FPS.

## Export a transfer kit

From the existing host checkout:

```powershell
./Modules/ASECM3/tools/Export-Kit.ps1 -Destination 'C:/M3Kit' -IncludeRuntime -IncludeLibraries
```

The destination must be new. The export contains this complete source module,
checksums, build/verification scripts, source identity, and a host-hook reference.
Optional runtime files come only from the staged M3 install manifest; optional
libraries come from the matching host build. Export verifies every copied file.
Game MPQs, account settings, model collections and saved absolute model paths
are not exported. Copy the desired models/animation banks/dependencies through
the destination client's supported local-file or MPQ source layout.

In the exported kit:

```powershell
./Verify-Kit.ps1
./Build-Core.ps1
# Optional portable source-data checks after building:
./Build-Core.ps1 -InspectModel 'D:/Models/base.m3' -AnimationBank 'D:/Models/required.m3a'
```

Build-Core discovers an installed Visual Studio C++ x64 toolchain with vswhere,
uses CMake/Ninja and performs no package downloads. Generated metadata and
vendored dependencies are included. Build artifacts go to a separate directory;
the source checksum manifest remains unchanged.

## Runtime overlay

When included, Runtime/Install-ASECM3.ps1 verifies its manifest and x64 PE image
architecture, backs up changed files and installs the integrated ASEC executable,
its matching XvidDecoder.dll, addon and private M3
texture dependencies into a supplied client root:

```powershell
./Runtime/Install-ASECM3.ps1 -ClientRoot 'D:/Client335'
```

Use a matching WoW 3.3.5a client-data installation with the x64 MSVC v143 runtime
(MSVCP140, VCRUNTIME140/140_1 and UCRT). The runtime is the integrated
ASEC client, including its other debug features. It is not an M3-only patch for
an arbitrary executable. Experimental rendering asset packs are separate from
this M3 overlay; Cataclysm water defaults off. Installation does not provide game
data or configure authentication. A fresh full-client runtime transfer still
requires validation; the core and kit checks alone do not establish that result.

## Native source integration

The exported host-hook reference records exact source hashes, function call
sites and surrounding code from the current fork. It is a review reference,
not a patch to apply blindly to another revision. The required contracts are:

1. Root CMake adds this module, links ASECM3Client into the native client and
   optionally adds ASECAssetViewer. With M3 disabled, Model2 links Disabled.cpp
   through ASECM3Disabled so hooks return to their original branches.
2. Unit model filename selection calls PrepareUnitModel before native model
   creation and AttachUnitModel after assigning the proxy, before effect
   recreation. Native M2 paths and shared model data remain owned by the host.
3. Unit destruction/replacement and WorldDestroy remove/reset module bindings
   before native object/device destruction. The movement update supplies time
   after native movement so pose/cloth inertia and immutable attachment snapshots
   are ready before native animation workers run.
4. Native action/emote selection supplies explicit requests to the module and
   applies its M3 readiness exception. Native M2 animation branches remain.
5. Model2 queues dedicated element type6 and dispatches the selected source
   packet with native pass/clip flags. Native group sorting and owner lifetime
   stay in the host; never take over the nameplate's draw callback.
6. Native bounds, picking and collision queries ask the module first, preserving
   their original distance/owner/priority contracts on successful dispatch and
   taking the original branch for ordinary M2 models.
7. Native attachment getters/visibility/child animation use immutable module
   snapshots. Source bone IDs never index placeholder arrays. Keep the normal
   0xFFFF sentinel and preserve native child ownership, detach and callback flow.
8. The host supplies compatible D3D9 state/cache structures, stream allocation,
   SFile filename resolution, large-texture staging and bounded diagnostics.
   The current source hash reference includes these supporting dependencies.

Validate startup, ordinary M2 rendering, M3-to-M2 transitions, actual world/DBC
selection, effects/attachments, physics, FPS, device recovery and clean shutdown
on the destination host. Current source material/VFX/layering/contact limitations
are described in the module README and verification status, and remain relevant
after copying the module.

Auxiliary material animation is module-owned too. EffectMaterials.md describes
its exact-record ownership checks and separate effect clock. Preserve the
explicit model-side effect dependency companions; automatic source actor phase
activation is not part of this route.
