# x64 nearby allocation regression

```powershell
cmake -S Tools/OsTests -B build/os-memory-tests -A x64
cmake --build build/os-memory-tests --config Release
ctest --test-dir build/os-memory-tests -C Release --output-on-failure
```

The test reserves a 3 GiB address window without committing it, leaving one 2 MiB hole
below a model anchor. It checks that a 128 KiB nearby allocation uses that hole, then
fills it and checks that an exhausted window returns failure. No client data, GPU,
or server connection is required. CTest limits the runtime to 15 seconds.

This reproduces animation loading stalls caused by querying virtual memory backward:
`VirtualQuery(address - 1)` describes the suffix starting at the queried page rather
than locating the beginning of the free region.
