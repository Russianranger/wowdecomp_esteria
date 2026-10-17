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
