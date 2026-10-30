# Compiled client manifest

Run after a successful matched client build:

```sh
python Tools/RuntimeManifest/generate.py --directory bin/Win/ARM64 \
  --architecture ARM64 --revision FULL_40_DIGIT_CLIENT_COMMIT \
  --build-id CI_RUN_AND_ATTEMPT \
  --descriptor-manifest build/client/generated/limits/limits.json
python -m unittest discover -s Tools/RuntimeManifest -v
```

The directory must contain `WowClient64.exe` and `XvidDecoder.dll` for the selected
architecture. The tool validates PE32+ header bounds, executable/DLL flags,
file-backed import descriptors and bounded ASCII DLL names without executing the
files. It requires the executable to import the packaged codec, checks the
generated build 12340 / 254-container / 25-quest / 32-class descriptor contract,
and streams SHA-256 hashes. It atomically writes `client-build.json`; failures
preserve any previous manifest.

`evidence: "build-only"` deliberately describes the result. Direct imports do not
include delay imports or libraries loaded by code. The manifest does not certify
runtime dependency completeness, descriptor agreement with a server, successful
startup, rendering, audio, login, or gameplay. Windows/Wine runtime qualification
remains separate. Synthetic tests need only the Python standard library.
