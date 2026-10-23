# Effect emission clock

EmissionClock compiles one explicitly bound particle owner and one explicitly
selected auxiliary effect sequence into an owned timeline. It never activates
an actor or selects the character's skeletal clip. The source Scene can be
released after construction. Source rate channels are resolved through the
new Scene::EffectScalarTrack ownership/collection-priority lookup; the existing
reference-aware scalar evaluator supplies values at curve knots.

## Continuous emission

emit_rate is interpreted as particles per second. Supported source reference
interpolation is constant (0) or linear (1). The curve is split at source key
times and clip boundaries and integrated analytically. Whole integrated units
produce requested particle births; each birth's sub-millisecond timestamp is
found by inverting the corresponding constant/linear segment integral. A
stable quadratic expression handles both increasing and decreasing rates.
Unanimated rate fields retain mesh defaults. Negative/nonfinite rates or
unsupported interpolation report Error rather than silently changing a curve.

IntegratedRate evaluates from the clip start, including complete source cycles
when the sequence loops. Advance compares cumulative whole counts rather than
accumulating a rounded per-render-frame rate. Fractional emission therefore
survives updates and source cycle boundaries. Frame partition does not change
the mathematical rate timeline. Source one-shot flags and zero-duration clips
follow the existing module's sequence policy.

## Authored count keys

Positive SDS6 emit_count keys request their integer quantity when the clock
crosses the key. Initial keys are included once; subsequent dispatch uses
(previous,current]. Duplicate simultaneous keys remain distinct. Source cycle
end and following cycle start keys are distinct occurrences at the same elapsed
boundary, consistent with the module event-clock convention. Negative count
keys are rejected. A nonzero static count without an animated channel reports
that explicit activation semantics are required; it is not silently emitted
every frame or discarded. This burst interpretation is adapter policy awaiting
a source asset with authored count keys, not established original-engine parity.

Reset begins a new explicitly activated phase. Backward clock movement requires
Reset. An error leaves the previous cursor/initial-state unchanged. Catch-up is
bounded to 4096 cycles and 4096 output occurrences/rate births per call. Curve
and count storage are bounded to 65536 keys each. Clock/integral ranges are
limited to the exact-integer range of double timestamps/counts. Output quantities
are requests; live-particle limits, lifetime, motion, dropped births and drawing
belong to the particle simulation/renderer, not this clock.

## Current evidence and integration limits

ASECM3Inspect <mesh.m3> --effect-clock <bank.m3a> explicitly binds the effects
dependency and reports each sequence/owner's integral and birth timestamps.
It compares whole-clip dispatch with 17 ms partitions. For all four supplied
death meshes, the Dissipate clip requests 202 ash and 120 smoke births. The
same 1288 total timestamps match independent integration of the actual source
curves read by io_m3, within the recorded JSON float tolerances. Native and
standalone module output match exactly after the final build.

The actual clip is one-shot and has no SDS6 count keys. These assets therefore
do not prove loop/burst/default-count behavior. No actor phase, particle lifetime,
motion, cap handling, native drawing, gameplay FPS or visual source parity is
established. No native client EXE was built/deployed or restarted. The accepted
cloth settings remain unchanged. The source clock is ready for an explicit
particle-simulation caller, but current runtime render paths do not call it.

Evidence under verification/m3: effects-emission-clock-{corpus,reference,
portable-parity}.json and effects-emission-clock-{build,final-build,
portable-build}.log.
