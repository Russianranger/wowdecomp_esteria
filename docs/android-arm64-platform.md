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

CMake 3.24+, Python 3.10+, Visual Studio 2022 C++ with ARM64 tools and Windows SDK:

```powershell
cmake -S . -B build/arm64 -G "Visual Studio 17 2022" -A ARM64 `
  -DWOWDECOMP_EXTENDED_LIMITS=ON -DWOWDECOMP_CONTAINER_SLOTS=254 `
  -DWOWDECOMP_QUEST_LOG_SLOTS=25 -DWOWDECOMP_MAX_CLASSES=32
cmake --build build/arm64 --config Release --target WowClient --parallel 4
```

ARM64 executable and matching Xvid DLL are staged together under
`bin/Win/ARM64`; x64 keeps `bin/Win`. Override
`-DWOWDECOMP_RUNTIME_OUTPUT_DIRECTORY=<absolute-path>` when packaging.
Use `-A x64` for the reference. ARM64EC is intentionally rejected pending its
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
headers. CI run `40981311211` passed all five component tests on Windows ARM64 as
well as Windows x64 and both Linux architectures. The complete x64 client and
Xvid DLL built successfully. The ARM64 full build reached one remaining x86
segment-register reference in exception formatting; that now reports ARM64's
actual fault address. Full ARM64 linking remains pending the next CI run.
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
