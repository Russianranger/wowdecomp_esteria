# Animation events and physics activation

Animation.cpp now retains typed SDEV event tracks and SDU3 unsigned tracks.
Events own their name/payload, ID, bone, flags, matrix, optional parameters and
original EVNT bytes. Integer keys stay uint32_t, avoiding precision loss above
2^24. PHRB v4 active-state reference IDs/defaults/nulls/header flags/interpolation
and blend-out fields are retained separately. Their state enum meanings are not
guessed or advertised as implemented.

Events.cpp dispatches the selected source STG/STC event tracks, honoring the
same collection priority rule as pose evaluation. Initial clip-start events are
inclusive once; later intervals are (previous,current]. Nonlooping clips fire
once; looping clips retain distinct end/start occurrences at a shared boundary.
Stable ordering preserves simultaneous keys. Catch-up is bounded by cycle,
occurrence and byte budgets. Clip selection resets event-start state.

GxScene exposes delivered count, last event and errors through GetAssetInfo.
A standard global Evt_Simulate (ID/bone -1, flags 4, identity matrix, no payload
or parameters) activates the model's private rigid world. It seeds at the exact
source event pose, derives linear/angular animation velocity from the preceding
up-to-eight milliseconds and advances only the remaining frame time. Repeated
activation of an already-active world is idempotent. Clip changes remove a world
created by an event. Manual ragdoll controls override automatic event activation
for that loaded model. Scoped/parameterized events remain explicit limitations.

## Source findings and evidence scope

The supplied 22 files contain 160 Evt_SeqEnd keys and **no Evt_Simulate keys**.
They also contain no SDU3 tracks. events-source-parity.json compares the source
track IDs, names/times and unsigned values with the independent local io_m3
reader. events-runtime-inspection.json covers clip partitioning and explicit
loop-boundary/nonloop checks; synthetic uint32 keys include values above 2^24.
These are source/timeline checks, not general actor execution.

The source catalog has RequiresPhysicsToOperate flags and separate actor-level
physics commands; those do not prove that every body should become dynamic.
Automatic HotS actor/state behavior remains required. The stock models are not
renamed or modified to inject missing simulation events.

make_event_probe.py creates a clearly labelled copy of the base death-ragdoll
model for native diagnostics. It changes an event name to Evt_Simulate and adds
a private 100-ms I32 time section, preserving original bone/material time arrays.
Its JSON records source and fixture hashes. The fixture is installed only under
ASECM3/ProbeFixtures, outside the original HOTS_M3 directory. A passing fixture
proves this activation path, not automatic activation of the unmodified corpus.
The labelled fixture and provenance JSON are now included in the M3 install
manifest (56 entries), so a transferred package can run the same native probe.
They are not dependencies for normal model rendering.

Native Glue evidence in runtime-events-probe.json/log passes stock SeqEnd
delivery, the derived fixture's event activation (16 bodies/14 joints, 13 steps),
clip-reset teardown and M3-to-M2 restoration (33 boundaries, zero failures).
The first probe failed because it did not start the fixture clip; that failure
is preserved in runtime-event-failed-1.log. The rerun explicitly starts clip 0
and retains the activation assertions. rigid-event-velocity-inspection.json
also checks velocity handoff on all 12 body-bearing mesh files by supplying an
explicit prior translated pose, separately from the source-only fixture motion.

Remaining: all source state enums and inherited state transitions, actor messages,
physics initialization/termination, selective body activation, scoped event
targets/transforms, event-specific payloads, concurrent/global clocks, layered
event ownership and exact source-engine response/visual acceptance.
