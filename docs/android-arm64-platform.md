# Android ARM64 client bring-up

Starting client revision: `800cdd272269570a9776d0c170e4828605fc1218` (Esteria tip verified 2026-10-29).
Development branch: `android/arm64-port`. Application integration belongs to
`Russianranger/esteria_android`, branch `android/client-server`.

## Selected path

First build the reconstructed source as a **Windows ARM64 executable**, retaining
its real Win32 window/input/fiber/network and reconstructed FMOD audio backends,
then qualify that executable inside a pinned local ARM64 Wine runtime. This is
Windows compatibility execution on Android, not a native Android application
binary. A successful Windows build does not establish Wine, graphics or game
compatibility. Keep an x64 build as the desktop reference; an unchanged stock
Wow.exe is never the reconstructed-client deliverable.

Native Android remains a subsequent path. CMake still deliberately rejects a
full non-Windows client: Win32 surfaces/loading, window events, WIC decoding,
OS services, fibers and audio output need real replacements before removing that
guard. The portable component test project already builds on Linux/ARM64 without
claiming to be a client.

## Initial implemented changes

- Separate instruction-set identification from 32/64-bit pointer layouts. ARM64
  is LLP64 like Windows x64; it must not link x64 libraries or execute CPUID.
- Model skinning uses a small SSE/NEON/scalar vector API, retaining operation
  order and supporting packed, unaligned model buffers. ARM64 uses real NEON.
  Voice conversions and reciprocal square roots have portable implementations.
- CPU discovery never advertises x86 MMX/SSE/TSC flags on ARM64. Win32 protocol
  platform identifiers remain unchanged; changing authentication fields requires
  matching protocol qualification.
- NVIDIA-only stereo integration is disabled at build time on ARM64. D3D and
  Vulkan rendering remain present. No audio or movie feature is discarded.
- Xvid movies use the official 1.3.7 source archive on ARM64, pinned by SHA-256 in
  `cmake/XvidPortable.cmake`. The upstream generic C codec is compiled; an
  affinity-mask DWORD/LP64 write bug is corrected in a build-tree copy. License
  is copied with build outputs. Existing x64 reference library remains unchanged.
- Appearance texture-bank LZNT1 decompression uses a bounded portable decoder,
  with malformed-input checks. WIC image conversion is still Windows-dependent.
- ARM64 crash contexts use PC/SP/FP and real register/minidump reporting.
- ARM64 MSVC explicitly uses `/volatile:ms` to preserve acquire/release ordering
  assumed by reconstructed Storm locks (for example `SUNNLockLeave`). ARM64's
  default ISO volatile semantics would not publish protected data safely.
  Migration to explicit atomic synchronization remains a separate follow-up;
  additional barriers can affect performance and require device measurement.

## Build and focused validation

CMake 3.24+, Python 3.10+, Visual Studio C++ tools and Windows SDK. The passing
native Windows ARM64 runner uses Visual Studio 2026; x64 uses Visual Studio 2022:

```powershell
cmake -S . -B build/arm64 -G "Visual Studio 18 2026" -A ARM64 `
  -DWOWDECOMP_EXTENDED_LIMITS=ON -DWOWDECOMP_CONTAINER_SLOTS=254 `
  -DWOWDECOMP_QUEST_LOG_SLOTS=25 -DWOWDECOMP_MAX_CLASSES=32
cmake --build build/arm64 --config Release --target WowClient --parallel 4
```

ARM64 executable and matching Xvid DLL are staged together under
`bin/Win/ARM64`; x64 keeps `bin/Win`. Override
`-DWOWDECOMP_RUNTIME_OUTPUT_DIRECTORY=<absolute-path>` when packaging.
Use `-G "Visual Studio 17 2022" -A x64` for the reference. ARM64EC is intentionally rejected pending its
own ABI qualification. First source-codec configuration downloads Xvid; subsequent
offline builds can provide an extracted official archive using
`-DFETCHCONTENT_SOURCE_DIR_XVID137=<path-to-xvidcore>`.

Portable behavior tests are independent of the incomplete client port:

```sh
cmake -S Tools/PlatformTests -B build/platform -DCMAKE_BUILD_TYPE=Release
cmake --build build/platform --parallel 4
ctest --test-dir build/platform --output-on-failure
```

Local Linux x64 evidence: six CTest cases pass (LZNT1 reference/malformed cases,
target architecture selection, 10,000 vector transforms with misalignment and guard regions on SSE and scalar,
voice rounding/overflow checks, real MPEG-4 encode/decode roundtrip, and the actual
movie adapter with red/blue BGRA orientation, output guards, skipped frames and
slot lifecycle). LZNT1 also
passed ASan/UBSan including 20,000 random inputs. Windows adds differential
comparison against system LZNT1 compression. `.github/workflows/arm64-client.yml`
builds/tests Linux x64/ARM64 and Windows x64/ARM64, then compiles the full client.
CI run `40980276079` passed Linux x64 and **native Linux ARM64** component
builds/tests. This validates NEON compilation/execution, the portable catalog
decoder and source-built MPEG-4 codec on ARM64, not Android/Wine gameplay.
Initial Windows failures exposed the Xvid generic MSVC inline-assembly guard
(fixed) and the hosted ARM64 image's migration to Visual Studio 2026 (workflow
now selects that generator). CI run `40980585693` additionally passed all five Windows x64 component tests,
including comparison against the system LZNT1 codec. Its clean full-client
configuration exposed mixed CRLF/LF hashes in ASECM3 metadata. The generator
inputs are now pinned to LF and metadata regenerated; only hash comments changed,
with the generated schema and semantic tables identical. The ARM64 runner also
exposed upstream Xvid integer macros colliding with the newer MSVC standard
headers. CI run `40982595595` at client commit
`a3683cc3644d965bf0ed8d91946cd5d6b3f928e1` passed all jobs: six component
behavior tests on Linux x64/ARM64 and Windows x64/ARM64, 17 manifest tests and
13 shader archive tests on Linux, and complete x64 and ARM64 client builds.
The downloaded ARM64 executable and Xvid DLL both have PE machine `0xaa64`.
Their direct CRT imports require `VCRUNTIME140.dll`; the client also imports
`MSVCP140.dll`. Neither directly imports the x64-only `VCRUNTIME140_1.dll` found
in the candidate Wine archive. The generated descriptor manifest has SHA-256
`16e5024377ebb4cd59b3fd0acb4029b35ae481f16c7bec39a5a6a9490867c188`.
These are compilation and component-level results; Wine startup, graphics,
audio, data loading, login and world entry remain separate qualification.
CI stages optimized `RelWithDebInfo` binaries and PDBs for useful crash reports;
release-only builds remain supported. `Tools/RuntimeManifest/generate.py` verifies
EXE/DLL machine types, checks generated descriptor limits, and records exact
source revision, build ID, hashes and direct imports as `client-build.json`.
Its evidence is explicitly build-only. Seventeen bounded parser/atomic-write
tests pass locally; the x64 artifact's 36 client and eight codec direct imports
also agree with independent `objdump` inspection. The movie adapter test also
accepts an absolute Windows DLL path and runs its fixture through the packaged
DLL's four exports. CI performs that check after binary architecture validation;
the test driver is included for later Wine qualification. Passing on Windows
still does not establish Wine/Android runtime behavior.

Run `40983191169` at `b335a3111cd64e69d5ceaebc73fb4daa57eed560` passed the complete
matrix plus the actual packaged DLL fixture on both native Windows architectures.
The subsequent `wine-arm64` job uses a fixed integration-repository commit and
this run's exact ARM64 artifact. It stages a hash-verified Wine candidate, executes
the real DLL fixture within an isolated native Linux ARM64 namespace and retains
logs/qualification JSON. The extended harness also runs the presentation and
texture lifecycle drivers against the candidate's pinned Lavapipe components,
requiring the actual reported software adapter and each success marker. Its
success must be read from the completed job; adding the workflow is not execution
evidence. Android runtime, full game startup and audio remain unqualified by these
component fixtures.

The first real native Linux ARM64 Wine codec run passed at client
`61ad13eb90c9d3c5db2c45d06f6c9da63a04cc81`, integration harness
`b7dd143f8e646f2d67606742e212c2dc744aee0d`, CI
[`40985737329`](https://github.com/Russianranger/wowdecomp_esteria/actions/runs/40985737329).
The packaged AA64 DLL decoded the fixture correctly with external networking
isolated, zero fixture/namespace exit, clean Wine stop and fully drained bounded
logs. [Evidence artifact 12946983350](https://github.com/Russianranger/wowdecomp_esteria/actions/runs/40985737329/artifacts/12946983350)
contains the receipt (SHA-256
`9e051e8b589cd2b2533aa1f7d9f9c938cfe2f79b11fba98bb52ae2e432fce562`).
Wine prefix setup returned success with nonfatal COM/device-service/optional
architecture diagnostics; this is a codec result, not qualification of every Wine
service. The game executable was hashed but not started. Both Windows architectures
also compiled the new graphics fixtures; their actual Wine execution is a separate
follow-up run, not established by this codec pass.

That follow-up passed at client `08e6d453733cff08f47eeb76dec5211749594753`,
integration harness `c2408d55862dfe4e299acf853443ebcedf0d2c94`, CI
[`40986534025`](https://github.com/Russianranger/wowdecomp_esteria/actions/runs/40986534025).
Both native Windows builds and the Linux ARM64 Wine codec/presentation/texture
fixtures passed. The real software adapter identified itself as llvmpipe LLVM
15.0.6; 16 cleared frames presented across a 248x158 to 376x222 resize, and all
30,000 image lifetimes were accounted for, including 20,000 simultaneous images.
The Khronos validation layer was absent and is explicitly reported unavailable;
this is not a validation-layer pass. [Evidence artifact 12947682663](https://github.com/Russianranger/wowdecomp_esteria/actions/runs/40986534025/artifacts/12947682663)
contains receipt SHA-256
`26e27a5437dd2785ed6b8db05b7281b7a45306696c1f58360709052f515cefc0`.
No game shaders, Adreno hardware, Android presentation or game data were exercised.

`Tools/VulkanTests` builds a clear/present fixture using the
engine's Win32 surface/device/swapchain and checked acquire/submit/present calls,
including resize. A new phase also exercises actual engine scene clears, partial
rectangles, exact 4x4 BGRA CPU readback and two presentations through its embedded
SPIR-V shaders. That phase has a separate success marker and harness scope;
readback precedes
the presentation shader and does not verify compositor pixels or data-derived
game shaders. The older texture fixture checks 30,000 resource allocations without
presenting. These fixtures do not establish world rendering; Lavapipe
software-driver results are not Adreno performance.

The engine-frame phase passed with both Windows builds at client
`69ccfaa6a0dd01607d0a99be9b27b7b25dafc5aa`, integration harness
`32329e64f05d9c9734b7c55c3270fb1800c7aff6`, CI
[`40992315512`](https://github.com/Russianranger/wowdecomp_esteria/actions/runs/40992315512).
The actual native Linux ARM64 Wine run observed the new exact marker and
`linuxArm64VulkanEngineFrameReadbackQualified=true`, checked the scene's red/blue
BGRA borders and green rectangles, and submitted both engine presentations.
Codec, executable entry and existing resource/resize tests also passed. Namespace
exit and expected worker exit were zero; bounded logs drained successfully.
[Evidence artifact 12949797521](https://github.com/Russianranger/wowdecomp_esteria/actions/runs/40992315512/artifacts/12949797521)
contains receipt SHA-256
`69bfe354c777abf78e54ed033849235928edf742efdfce3371bca4c1e01379c4`.
This still used llvmpipe LLVM 15.0.6, with no validation layer, Android surface,
Adreno hardware, compositor-pixel readback or full game rendering qualification.

## Actual executable entry diagnostic

The explicit single argument `--esteria-entry-check` runs a bounded diagnostic
through the **actual** 64-bit executable's `/ENTRY:start`, Storm/CRT setup,
`WinMain` and newly-created main fiber. It checks initialized CRT floating-point
control, real allocation/reallocation/preservation/free, a cross-function caught
C++ exception with destructor unwinding, and return to the caller fiber after
the main fiber is deleted. It returns before `CommonMain`: no MPQ, game systems,
audio, renderer, server or login is initialized. Normal invocation retains the
existing `CommonMain` path.

Success exits 0 and exclusively creates a small sibling `Esteria-entry-check.json`
with scope `executable-entry-only`. The complete flag with a case variation,
appended value or additional arguments exits 64; unrelated ordinary client
arguments retain their existing handling. An existing receipt is preserved and causes exit 66. Select a
fresh writable binary directory for each attempt. The production crash logger is
still active; its console may affect stdout, so the receipt plus process exit,
not a printed marker alone, establishes success.

```powershell
python Tools/PlatformTests/verify_client_entry.py --directory bin/Win/ARM64 `
  --architecture ARM64 --output build/client-entry
```

The Windows matrix verifies native architecture and binary hashes, copies the
matched EXE/DLL to a private test directory, checks rejected arguments, valid entry
and repeat-receipt preservation, and retains bounded diagnostics. Its helper tests
run on Linux without pretending to execute the client. The optional Wine harness
exercises the same entry mode separately from codec or graphics tests.

The native Windows ARM64/x64 entry checks and native Linux ARM64 Wine entry check
passed at client `f75c2c71d9bb76a6707bfb68b2bf615db544e5bf`, integration harness
`9ee80cd31b079e6dab8b41de2429030447665cf5`, CI
[`40989834370`](https://github.com/Russianranger/wowdecomp_esteria/actions/runs/40989834370).
All five jobs passed, including codec and existing software Vulkan fixtures.
[Wine evidence artifact 12949535366](https://github.com/Russianranger/wowdecomp_esteria/actions/runs/40989834370/artifacts/12949535366)
contains qualification receipt SHA-256
`a7686069cd8e62eb8cb2075380e5cbb7390b2e42718c318413261cffc54141a6`
and entry receipt SHA-256
`0ed74a49d5f65c792391394fda20187fe07e9d99d3f6de582db17a07e25d356b`.
All seven checks are true, the worker/namespace exit is zero, and
`commonMainStarted`/`assetsLoaded` remain false. This is executable-entry
qualification, not normal game startup.

The real crash logger starts under Wine and captures the current program counter,
but its DbgHelp ARM64 walk stops after one frame with an unavailable-metadata/end
of stack diagnostic. Complete Wine stack unwinding remains unqualified. The
successful C++ exception/destructor test is separate evidence and does not resolve
that diagnostic limitation.

## Captured FMOD output qualification

`Tools/AudioTests` links the same reconstructed `fmodex` source target as the full
client. Its explicit WinMM and DirectSound cases require an output device,
reject a no-sound fallback, and check progression/completion of a known two-second
stereo signal. The paired native Linux ARM64 Wine job builds the exact integration
checkout's pinned guest ALSA plugin and captures its actual accepted PCM through
a bounded receiver. Both channel frequencies, amplitude/duration, lifecycle
markers and owned cleanup must pass. The receiver supplies a simulated monotonic
clock; this cannot qualify Android AudioTrack, audible hardware output, physical
latency, music decoding or complete game audio. The first complete captured-FMOD
execution is pending; the existing codec, entry and graphics evidence remains
scoped separately.

## Optional Retail/Forever path

The primary artifact explicitly uses `WOWDECOMP_RETAIL_ASSETS=OFF`. Enabling it
builds CascLib from source commit `38a34665624b8775bb875274b36191b21c38d97b`, with
the source archive SHA-256 pinned in `cmake/RetailAssets.cmake`. ARM64 compilation
of this optional path has not yet been qualified.

`RetailCasc.ini` and the matching Forever source support `mode=files`: pinned
build config, locale and FileDataID `.bin` files are required. This mode reads the
local pack and returns an error for missing assets; it does not fall back to CDN
access. `mode=online` explicitly permits network acquisition and belongs only in
setup/update tooling for the Android application. Actual dependency closure,
matching table overlays and custom rendering still require the unavailable
content package and separate validation.

## Remaining qualification

Do not label the client playable until full-client builds, Wine startup,
effective-data shader generation, rendering/audio/input, login and world-entry,
Esteria extensions, and offline gameplay have been verified. The exact runtime,
application package and server pins are owned by the integration repository.
No user device test is requested for this component checkpoint.

Reference sources: [Microsoft ARM64 intrinsics](https://learn.microsoft.com/cpp/intrinsics/arm64-intrinsics),
[MSVC volatile ordering](https://learn.microsoft.com/cpp/build/reference/volatile-volatile-keyword-interpretation),
[official Xvid source](https://labs.xvid.com/source/),
[GitHub ARM64 hosted runners](https://github.blog/changelog/2025-08-07-arm64-hosted-runners-for-public-repositories-are-now-generally-available/).
