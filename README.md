# wowdecomp

A C++ decompilation of the World of Warcraft 3.3.5a client (`Wow.exe`, build 12340, June 2010).

Join the discord for other clients and to show off some cool stuff you build.

https://discord.gg/DsZMGUD8aT

Every function is reconstructed from the original binary and keeps the address it came from in an
`// OFFSET:` tag, so each one can be compared against the original. The source tree follows the layout
of the original client (`Engine/Source/<Module>`, `WoW/Source/<Module>`, original file names).

x64 is the main target. Win32 still builds and is used to check struct layouts against the 32-bit
original.

## Progress

The percentages below measure how much of the original binary has a counterpart in this source tree. They do
**not** say how much of the game works, and the missing part is not missing gameplay: the client builds into a
complete executable and the game is 100% playable. Bugs could still be hiding, though; a ported function can behave differently
from the original.

Measured against the 12340 binary on 2026-10-15. A function counts as ported when its address is
tagged in the source.

| | Ported | Total | Share of the original |
|---|---:|---:|---:|
| Functions | 24,515 | 27,157 | 90.3% |
| Code bytes | 5,270,186 | 5,656,573 | 93.2% |

The total leaves out 658 functions that IDA identifies as compiler library code and 132 import thunks.
Most of the untagged remainder is code the build gets elsewhere: compiler-generated code (static
initializers, template instances) that our compiler emits on its own, C runtime helpers, and third-party
libraries that are linked from their upstream sources instead of being decompiled (FreeType, zlib, PCRE,
parts of FMOD Ex).

## Requirements

- Windows 10 or 11
- Visual Studio 2022 with the **Desktop development with C++** workload (MSVC v143 and a Windows SDK)
- CMake 3.10 or newer (the one that ships with Visual Studio works)
- For Win32 builds only: [NASM](https://www.nasm.us/), on `PATH` or passed as `-DNASM_EXECUTABLE=<path>`, and
  `DivxDecoder.dll` from your 3.3.5a client copied to `ThirdParty/DivxDecoder/` (the build copies it next to the
  executables; the x64 build uses its own decoder instead)
- A World of Warcraft **3.3.5a (12340)** client. No game data is included in this repository.

## Building

From a Developer PowerShell or any shell with `cmake` on `PATH`:

```powershell
git clone https://github.com/SatyPardus/wowdecomp.git
cd wowdecomp

# x64 (main target)
cmake -S . -B build/x64 -G "Visual Studio 17 2022" -A x64
cmake --build build/x64 --config Release --target WowClient --parallel

# Win32 (optional, needs NASM)
cmake -S . -B build/win32 -G "Visual Studio 17 2022" -A Win32
cmake --build build/win32 --config Release --target WowClient --parallel
```

In-source builds are refused, so keep the build directory separate (`build/` above).

The client is written to `bin/Win/`:

| Target | x64 | Win32 |
|---|---|---|
| `WowClient` | `WowClient64.exe` | `WowClient32.exe` |
| `Wow` | `Wow64.exe` | `Wow32.exe` |

`WowClient` is the client with a crash logger: it opens a console window and writes
`WowClient_crash.log` when the client crashes. `Wow` is the same client without it. You can also turn the
logger off in `WowClient` with `-DWOWCLIENT_CRASH_LOG=OFF`.

### Options

| CMake option | Default | Effect |
|---|---|---|
| `WOWDECOMP_DISABLE_SIGNATURE_CHECKS` | `OFF` | Skip the MPQ file and `.toc.sig` signature checks, so modified game data and interface files load |
| `WOWDECOMP_EXTENDED_LIMITS` | `OFF` | Opt into custom player/container layouts; requires a matching server |
| `WOWDECOMP_QUEST_LOG_SLOTS` | `25` | Active quest ceiling, up to 1000 with extended limits |
| `WOWDECOMP_CONTAINER_SLOTS` | `36` | Container ceiling, up to 254 with extended limits; adds no bag content or art |
| `WOWDECOMP_MAX_CLASSES` | `32` | Maximum class ID, up to 254 with extended limits; widens class masks and requires matching server/DBC formats |
| `WOWDECOMP_MAX_UPDATE_MASKS` | derived (`42` stock) | Accepted mask blocks; 1000 quests require 194 |
| `WOWCLIENT_CRASH_LOG` | `ON` | Build `WowClient` with the crash logger |
| `WOWCLIENT_OUTPUT_OVERRIDE` | empty | Name the `WowClient` executable something other than `WowClient64` / `WowClient32` |
| `WOWDECOMP_EXTENSION_HELLOWORLD` | `ON` | Compile the imported extension hook examples into the client |

See [extended limits](docs/uncaps.md) for the 1000-quest/254-class configuration, generated
server field contract, and pending server/UI acceptance work. Extended descriptor
generation requires Python 3.10 or newer; stock builds do not require Python.

## Running

1. Copy the `Data` folder of your 3.3.5a client into `bin/Win/`, so you have `bin/Win/Data/`.
2. Point `Data/<locale>/realmlist.wtf` at the server you want to connect to.
3. Start `bin/Win/WowClient64.exe` (or `Wow64.exe`).

The built-in cursor selector is under **Escape → Interface → Mouse → Cursor appearance**.
Human (Default), Night Elf, Orc, Undead, Hearthstone, and animated Starcraft apply
immediately and save between sessions.
See [native cursor textures](Addons/Cursors/README.md) for conversion and behavior details.

### Extensions and Vulkan

The extension hook system from [upstream PR #10](https://github.com/SatyPardus/wowdecomp/pull/10)
discovers extensions under `Extensions/<Name>/src/`. Each has a
`WOWDECOMP_EXTENSION_<UPPERCASE_NAME>` CMake option; the bundled `HelloWorld` extension
demonstrates the hooks and writes to `Logs/Extensions.log`.

The experimental Vulkan backend from [upstream PR #11](https://github.com/SatyPardus/wowdecomp/pull/11)
can be selected with `-vulkan` or the `gxApi` CVar set to `Vulkan`. D3D11 remains available
with `-d3d11`. Vulkan requires a driver providing `vulkan-1.dll`; building it uses the
bundled headers and requires a C++20 compiler, without requiring the Vulkan SDK.
For world and model shaders, follow the [offline Vulkan shader translation instructions](Tools/VulkanShaders/README.md)
using your own client data. Vulkan remains work in progress; build success does not establish
rendering or gameplay correctness.

## Layout

| Path | Contents |
|---|---|
| `Engine/Source/` | Engine modules: Storm, Gx (Direct3D 9 / Direct3D 11 / OpenGL / Vulkan), Model2, Frame/FrameXML UI, SoundEngine, Lua, ... |
| `WoW/Source/` | Game modules: objects, world rendering, UI, networking, database, glue screens |
| `WoW/Common/` | Code shared by the game modules |
| `ThirdParty/` | Libraries linked from their upstream sources |
| `Tools/WowClient/` | The `WowClient` executable target |
| `Tools/XvidDecoder/` | x64 movie decoder that replaces the original 32-bit `DivxDecoder.dll` |

## Special thanks

- [binana](https://github.com/thunderbrewhq/binana)
- [thunderbrew](https://github.com/thunderbrewhq/thunderbrew)
- [whoa](https://github.com/whoahq/whoa)
- TOMRUS, for the initial IDB
- Everyone who has reported bugs so far
- The WoW modding community
