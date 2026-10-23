# Explicit PHSH convex collision shapes

`ConvexPhysics.cpp` is an ASECM3Core object. The module's pinned producer schema
(`schema/structures.xml`, PHSH) identifies shape 4 as convex hull and shape 5 as
mesh. This lane handles shape 4 only; it does not reinterpret mesh collision as
a hull or replace unavailable geometry with a primitive.

## Geometry ownership and frame

The source PHSH vertex set defines the hull. Bullet's vendored
`btConvexHullComputer` constructs its face loops. Its original-vertex-index map
selects the corresponding scale-baked source points, so generated/quantized hull
coordinates are not silently substituted for the supplied vertex coordinates.
No source face-data layout or unknown topology fields are guessed.

The serialized local affine matrix and the body's positive bind scale are baked
into these points once, including translation. This preserves the ordering of
nonuniform bone scale and local shape rotation. Convex local shear/reflection
can therefore be represented by the point set; the existing body-bone frame
decomposition restrictions still apply. Primitive shape behavior is unchanged.

## Volume, centre and inertia

Polygon faces are triangulated as fans. Signed tetrahedra about the average hull
vertex produce double-precision volume, first moments and second moments. The
reference offset reduces cancellation for translated shapes. A consistent
negative face winding is normalized once for all accumulated moments.

For a tetrahedron `(0,a,b,c)` with signed volume V:

- first moment: `V*(a+b+c)/4`;
- second product moment `(i,j)`:
  `V*((a+b+c)[i]*(a+b+c)[j] + a[i]*a[j] + b[i]*b[j] + c[i]*c[j])/20`.

The central inertia tensor is `trace(S)*I-S` after subtracting the centre's
parallel-axis contribution. Bullet's Jacobi diagonalizer supplies principal
axes; reconstruction using only the resulting diagonal is checked against the
original tensor. A child collision shape uses centred principal-axis vertices,
the corresponding body-local transform, and a custom `calculateLocalInertia`
that returns the integrated principal inertia times child mass. This avoids
Bullet's generic convex-shape AABB inertia approximation.

Existing density-times-volume and compound principal-mass-frame integration
then place the body and its joints. Collision margin remains 0.005, matching
the existing preview policy; mass moments use the uninflated geometric hull.
Uniform density, the meaning of the serialized density field, and source material
preset/overlap policies retain their existing verification limits.

## Bounds and failure behavior

- 4-8192 source points; finite positive bone scale and finite affine local frame.
- Baked coordinates bounded to +/-1,000,000 body-local units.
- At most 8192 hull vertices, 65536 edges and 32768 polygon faces.
- Every output-to-source vertex index and polygon loop is checked.
- Volume must be finite with magnitude in `[1e-12,1e12]`.
- Principal moments must be finite/positive; tensor reconstruction residual is
  limited to `max(1e-8, maximum tensor entry * 1e-4)`.
- Empty, collinear/coplanar, invalid, excessively large or unsupported geometry
  reports through the existing rigid-simulation error lane. Render geometry is
  retained. No fallback hull/box is fabricated.

## Current evidence

Native and standalone ASECM3Core libraries compile. No tests were added or run
for this change. The prior supplied-corpus evidence exercises primitive physics,
not this hull lane. There is no claimed analytic-fixture, PHSH hull asset,
simulation, live FPS or original-engine parity result for the new code yet.

This addition is source/library work. The current installed water-comparison EXE
does not contain it; no new full client was produced for this addition. The v12
source kit also predates it. Integration into a future compiled client and runtime
acceptance remain outstanding.
