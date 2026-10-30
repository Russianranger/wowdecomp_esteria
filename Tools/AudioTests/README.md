# Reconstructed FMOD output fixture

Builds the same `fmodex` source target as the client, without game content or the
higher-level game sound system. It selects a real WinMM or DirectSound output,
generates two seconds of stereo PCM (1 kHz left, 2 kHz right), and checks actual
device enumeration, selected backend, advancing sample clock, completion and
release. Missing output devices and a no-sound fallback fail the fixture.

```powershell
cmake -S Tools/AudioTests -B build/audio -G "Visual Studio 18 2026" -A ARM64
cmake --build build/audio --config Release --parallel 4
build/audio/Release/fmod_output_device.exe --winmm
build/audio/Release/fmod_output_device.exe --dsound
```

Use the x64 Visual Studio generator for the desktop reference. These commands
need an actual output provider; hosted Windows runners are not assumed to have
one. Build evidence alone cannot establish audio execution.

The success marker establishes only the FMOD API/clock lifecycle. Qualification
also requires independently captured PCM with the expected channel frequencies,
amplitude and duration. The integration repository's bounded PCM receiver will
exercise the real Wine/ALSA transport against a simulated monotonic playback
clock. Such a run cannot establish Android AudioTrack, physical output latency,
audible speaker output, music decoding or complete game audio. Both Windows
compilation and the paired Wine transport run must pass before qualification.

The client Wine job checks out integration harness
`61bf8a134845276cb78430884d557e477ff286aa`, rebuilds its guest helpers from pinned
Debian ARM64 inputs, and verifies the resulting source/ELF receipt before use.
The harness explicitly selects Wine ALSA and the private source-built plugin,
runs both output backends, and exports the raw bounded PCM captures with hashes.
It requires both exact lifecycle markers, correct captured stereo content and
clean owned shutdown for `linuxArm64FmodPcmTransportQualified`. This new execution
path is pending its first actual CI result; adding it does not establish audio.
