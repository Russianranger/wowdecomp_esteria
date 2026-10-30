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

## Build and focused validation

CMake 3.24+, Python 3.10+, Visual Studio 2022 C++ with ARM64 tools and Windows SDK:

```powershell
cmake -S . -B build/arm64 -G "Visual Studio 17 2022" -A ARM64 `
  -DWOWDECOMP_EXTENDED_LIMITS=ON -DWOWDECOMP_CONTAINER_SLOTS=254 `
  -DWOWDECOMP_QUEST_LOG_SLOTS=25 -DWOWDECOMP_MAX_CLASSES=32
cmake --build build/arm64 --config Release --target WowClient --parallel 4
```

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

Local Linux x64 evidence: five CTest cases pass (LZNT1 reference/malformed cases,
target architecture selection, 10,000 vector transforms with misalignment and guard regions on SSE and scalar,
voice rounding/overflow checks, real MPEG-4 encode/decode roundtrip). LZNT1 also
passed ASan/UBSan including 20,000 random inputs. Windows adds differential
comparison against system LZNT1 compression. `.github/workflows/arm64-client.yml`
builds/tests Linux x64/ARM64 and Windows x64/ARM64, then compiles the full client.
CI run `40980276079` passed Linux x64 and **native Linux ARM64** component
builds/tests. This validates NEON compilation/execution, the portable catalog
decoder and source-built MPEG-4 codec on ARM64, not Android/Wine gameplay.
Initial Windows failures exposed the Xvid generic MSVC inline-assembly guard
(fixed) and the hosted ARM64 image's migration to Visual Studio 2026 (workflow
now selects that generator). Windows full-client builds remain pending.

## Remaining qualification

Do not label the client playable until full-client builds, Wine startup,
effective-data shader generation, rendering/audio/input, login and world-entry,
Esteria extensions, and offline gameplay have been verified. The exact runtime,
application package and server pins are owned by the integration repository.
No user device test is requested for this component checkpoint.

Reference sources: [Microsoft ARM64 intrinsics](https://learn.microsoft.com/cpp/intrinsics/arm64-intrinsics),
[official Xvid source](https://labs.xvid.com/source/),
[GitHub ARM64 hosted runners](https://github.blog/changelog/2025-08-07-arm64-hosted-runners-for-public-repositories-are-now-generally-available/).
