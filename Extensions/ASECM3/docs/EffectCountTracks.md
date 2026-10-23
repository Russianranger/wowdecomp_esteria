# Signed effect-count tracks

The portable animation decoder now retains channel type 7 (STC.sds6,
SDS6.keys -> I16_) in TransformCollection.signed16Values. This follows the
checked-in generated schema and the supplied M3.bt SDS6/I16_ structures.
Particle emit_count references have int16 defaults in the generated schema.

Each Signed16Track owns its ID, flags, end timestamp, frame timestamps and
signed 16-bit values. Negative values are explicitly sign-extended. Source
key order and duplicate timestamps are retained; decoding does not clamp,
interpolate, sum or discard authored count keys. Frame/key extent checks and
the existing 16-million-key decoded-animation budget apply to these tracks.

Effect-bank compatibility now includes these channels when checking active
parameter reference interpolation agreement. Scene::EffectCountTrack resolves
only a validated bound particle owner and int16 parameter, then selects the
selected group's channel with the existing collection-priority convention
(later collection wins equal priority). The returned pointer is borrowed for
the lifetime of the owning Scene. A missing channel returns null, leaving the
mesh's authored default available. Invalid owners, field types and groups throw
the module's contextual Error instead of interpreting unrelated IDs as counts.

This API exposes source keys. It does not implement burst scheduling, clock
crossings, loop boundaries, actor activation or particles. In particular,
sampling a count every rendered frame would incorrectly repeat a burst; no
such behavior has been added.

ASECM3Inspect --signed-tracks emits collection index/name, ID, flags, end and
exact [timestamp,value] keys in deterministic collection/ID order. Detailed
output is limited to one million keys. Normal inspector output is unchanged.
Its usage text also now documents --effect-declaration.

Native core/client libraries and the standalone inspector compile. Inspection
of the 22 supplied files succeeds with zero signed-count tracks. That corpus
provides parse-regression evidence only, not real signed-count playback or
integer boundary coverage. A real asset with authored SDS6 count channels is
still needed for independent source-key comparison and later burst playback.
No native client executable was built, installed or restarted in this step.
The accepted cloth solver settings were not changed.
