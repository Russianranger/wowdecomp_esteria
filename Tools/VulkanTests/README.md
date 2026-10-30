# Vulkan texture regression tests

From the repository root on Windows with MSVC and CMake:

```powershell
cmake -S Tools/VulkanTests -B build/vulkan-texture-tests -A x64
cmake --build build/vulkan-texture-tests --config Release
ctest --test-dir build/vulkan-texture-tests -C Release --output-on-failure
```

The registry test needs no Vulkan driver or client data. It tracks 40,000 synthetic image
handles, crosses the former 16,384-image limit, checks allocation failure rollback, and
verifies lookup, metadata, deletion, wrapped probe chains, and reinitialization.

Pass `-DVULKAN_TEXTURE_DEVICE_TEST=ON` when configuring to also run a driver test.
It opens a hidden window, creates 20,000 simultaneous one-pixel images, releases and
replaces alternating images, then checks that all 30,000 allocations were destroyed.
It requires a Vulkan-capable GPU and driver but no client data or server connection.

`-DVULKAN_PRESENT_DEVICE_TEST=ON` also builds `presentation_device`. It uses the
engine's actual dynamic Vulkan loader, device selection, Win32 surface and
swapchain. It acquires, clears, submits and presents eight alternating red/blue
frames, resizes the window, rebuilds the swapchain and presents another eight.
Every returning Vulkan operation, completion fence, present result and changed
extent is checked. Renderer/validation callback errors fail the fixture, including
teardown errors. It reports whether the validation layer and callback were actually
enabled; absence is not called a validation-layer pass. No game data or shader compilation is needed; a render-pass
clear supplies the image contents.

It then initializes the engine memory/texture/frame/present modules, clears the
real scene target red and blue with a green rectangle inside each render pass,
and checks every BGRA channel in two 4x4 CPU readbacks. Two additional frames go
through the engine's actual embedded presentation shaders. The fixture checks
submitted/presented counters, readback completion and complete texture cleanup.
This phase emits its own exact marker for separate harness qualification.

The fixture establishes clear/submit/present, resize and engine scene readback
behavior. Readbacks precede the presentation shader; they do not read compositor
pixels, run data-derived game shaders or measure performance. Software
Lavapipe execution under Linux ARM64 Wine is separate from Windows GPU execution
and Android/Adreno qualification. `texture_device` only initializes a swapchain
and checks resource allocation; it submits no frames and cannot establish
presentation by itself. Both device fixtures fail when no suitable runtime or
driver is available, rather than skipping and reporting a pass.

Build Windows ARM64 fixtures with the hosted runner's generator:

```powershell
cmake -S Tools/VulkanTests -B build/vulkan -G "Visual Studio 18 2026" -A ARM64 `
  -DVULKAN_TEXTURE_DEVICE_TEST=ON -DVULKAN_PRESENT_DEVICE_TEST=ON
cmake --build build/vulkan --config Release --parallel 4
```

The Windows CI matrix builds the two device binaries and runs the driver-free
registry test. Hosted Windows images are not assumed to provide Vulkan hardware;
the device fixtures are retained for explicit runtime execution.
