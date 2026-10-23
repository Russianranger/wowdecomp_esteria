# Observed cloth collision-volume motion

The previous solver interpolated every PHCC capsule matrix entry between two
observed poses. Averaging two proper rotation matrices generally produces a
contraction rather than a rotation; at a half-turn midpoint it can become
singular. This changes the collision volume between fixed ticks and can cause
penetration followed by a larger projection when its size returns.

`AffineMotion.cpp` is a new portable module object. It separates the endpoint's
linear part into a proper rotation and symmetric stretch using the vendored
Bullet polar-decomposition implementation. Both endpoints retain their original
matrices at weights zero and one. Between them, translation/stretch interpolate
linearly and normalized quaternion rotation follows the shortest arc. Positive
definite stretches interpolate within their positive definite cone. A constant
reflection is represented consistently by signed stretch; changing handedness
is rejected rather than interpolated through a singular volume.

Finite affine endpoints, coordinate/linear-entry bounds of 1e6, relative
determinant conditioning, 32 bounded decomposition iterations, orthogonality,
reconstruction and positive-stretch checks guard the input. Errors identify the
PHCC index through the existing cloth error lane. Authored singular/zero-scale
endpoints disable collision for that observed interval, as zero-scale collision
volumes did previously; they do not fabricate a capsule at the origin.

ClothSimulation factors each capsule interval once per update containing fixed
ticks and reuses it at the same fixed tick/remainder fractions as before. The
existing contact sweep uses the preceding tick's sampled transform. No additional
source file, original host hook, per-vertex decomposition, solver rewind or native
engine pointer is introduced. Existing gravity, damping, stiffness, tracking,
wind, mass, tethers, friction and projection counts are unchanged.

This is improved interpolation between observations, not proof of the source
Domino solver's substep trajectory. Native/portable libraries compile. No tests
were added or run. Appearance, contacts and frame cost need a current-build
gameplay comparison; prior accepted cloth appearance belongs to EXE 5A424ED7.

The user subsequently reports 40-50 FPS with M3 versus 64 without on EXE 13E2D6B0.
Those logs show aggregate update and draw costs; they do not isolate this change.
The next build detects orthogonal rotation/axis-scale endpoints directly, avoiding
iterative polar inversions for those common frames. Sheared frames retain the
polar path, and reconstruction/positive-stretch checks still apply to both. This
reduces one source of work without reducing solver frequency or projection counts;
it is not an FPS acceptance result or proof that the full regression is resolved.
