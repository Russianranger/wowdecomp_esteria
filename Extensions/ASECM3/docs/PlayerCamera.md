# Player camera fallback

When the active player's model has no attachment17 with a finite positive Z
pivot, the camera uses:

    max(0, topZ - 0.1 * (topZ - bottomZ)) * playerScale

This measures height relative to the model origin. It uses real M2 vertex
positions or stable default-pose M3 mesh vertices, excluding validated physics
helper regions. Particle clouds and collision-box dimensions do not set the
pivot. Stable bounds prevent animation and cloth flutter from moving the eye
height each frame. A positive camera attachment retains native behavior.

CameraHeight.cpp owns the portable bounds/formula. PlayerCamera.cpp owns the
native selection/M2 scan; UnitWorldBinding snapshots M3 bounds on the parser
worker and publishes them through the existing ready flag. Camera initialization
waits for M3 source readiness rather than using ErrorCube bounds. Empty or invalid
data yields a zero-origin fallback; failures reach built-in diagnostics. M2 scans
are capped at two million vertices. Logs/ASECM3-camera.log records the selected
attachment/bounds branch and calculated values at camera initialization.

The Camera.cpp host hook queries this module before marking a target loaded.
For bounds fallback it bypasses the original character's collision-height cap
and minimum0.8333 eye height, retains mount-seat offsets, and clamps final near,
far and swimming targets to zero. Placeholder sequence bounds do not adjust
the fallback. Normal attachment-based camera initialization stays native.

Hosts enabling this feature must carry the small Camera.cpp integration shown
in host-hook-reference.json. The disabled module boundary returns native behavior.
This is an installed client feature, not a Lua overlay or persistent CVar tweak.

The client compiles and the installed runtime hashes are checked. No camera
tests were added or run for this request. Actual in-world eye height, loading
transition, mount/scale handling and native-M2 cases need live confirmation.
