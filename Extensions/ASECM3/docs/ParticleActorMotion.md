# Actor transforms at particle birth

`ParticleTransformInterval.cpp` is a separate portable core object. It owns the
two observed transforms bounding an update and samples only inside that time
range. Translation and uniform scale interpolate linearly; rotation uses the
shortest quaternion arc. Observed endpoints are returned exactly. A zero-duration
observation retains its latest transform. Uniform reflections retain their
handedness. Nonuniform, skewed, singular or nonfinite matrices, changing
handedness, invalid times and extrapolation reject explicitly.

This is an adapter interpolation policy between observed actor poses, not a
claim about HotS's interpolation algorithm. A long unobserved turn may have made
more than half a revolution; endpoints alone cannot reconstruct that motion.
Teleport/discontinuity-specific source activation and substep physics histories
remain work to do.

The native GxParticles adapter retains the previous actor transform only for an
active effect phase and initializes it from the current transform on activation.
Each accepted birth receives its own observed-interval transform through
ParticleEmitter::AdvanceSampled. Dropped births cause no surface lookup. A source
surface is still built once lazily per emitter/update from the current solved
mesh, so this change does not re-evaluate or rewind cloth/ragdoll physics. Full
source-pose-at-birth fidelity remains incomplete.

The existing local/world simulation distinction remains. World particles retain
birth position, velocity transform and visual scale; local particles follow the
current actor transform at snapshot time. Stopping/restarting a phase clears the
history and counters. Native preview/world info exposes birth sample counts;
existing periodic world logs include them without per-birth logging.

## Verification

The transform inspector checks 168 analytic samples across three starting
orientations, four rotation axes, reflected and proper transforms, exact
endpoints, and whole versus split intervals. Maximum matrix component errors
are about 1.193e-7. Nine invalid-input checks reject and a zero-duration check
retains its newest observation. Native/standalone outputs match. Existing twelve
particle/material model-mode outputs match in both core builds. Those static
corpora do not exercise the new native motion callback.

The native pre-login probe additionally requires nonzero actor-history birth
samples before recording M3_BIRTH_TRANSFORM_PREVIEW_PASS. Its pass is submission
and registration evidence, not moving-world appearance/FPS or source physics
parity. Consult the exact native result and current STATUS.md for release state.

Evidence: verification/m3/effects-birth-transform-{math,portable-parity,
regression,native-result}.json and corresponding build/probe logs.
