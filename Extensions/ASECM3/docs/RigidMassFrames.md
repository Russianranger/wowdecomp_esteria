# Principal mass frames for rigid bodies

The source shape matrices are bone-local, and many primitive centers are offset
from their bone pivot. A Bullet rigid body expects its origin at its center of
mass and a diagonal inertia tensor in its local principal frame. Treating the
bone pivot as that frame produced incorrect rotational response.

`RigidMassProperties.cpp` is a separate portable module object. It computes
per-child masses from the existing source-density/primitive-volume policy, calls
the vendored Bullet compound principal-axis implementation, checks finite positive
mass/inertia and bounds, then recenters privately owned collision children.
The Scene and authored source records are unchanged.

The returned `centerInBone` maps center-frame coordinates into the original
scaled bone-local frame. The runtime composes the original bone world transform
with this frame for solver initialization and kinematic targets, and applies its
inverse when publishing solved bone transforms. Bone-local joint frames are
converted by its inverse after checking the original authored bind pivots.
Animation-to-physics velocity handoff uses the centers' world positions as well
as orientations, preserving the tangential velocity of an offset center during
rotation. Resets and normal/tangent skinning follow the corrected bone frame.

The underlying reference is the vendored
third_party/bullet/src/BulletCollision/CollisionShapes/btCompoundShape.{h,cpp},
calculatePrincipalAxisTransform. Its API requires both inverse child-frame
correction and corresponding body-world correction; changing inertia alone
would move collision shapes or detach joints.

## Verification and limits

The two analytic fixtures contain unequal masses on translated solid boxes,
with one compound additionally rotated and translated. An independent NumPy
audit computes the weighted center, box inertia, rotated tensor and parallel-axis
terms. Maximum center error4.34e-7, tensor component error1.50e-5; reconstructed
collision-corner displacement2.31e-6. Five invalid mass inputs reject.

All18supplied mesh files pass the rigid inspector. Body/dynamic-body/joint counts
are unchanged from a fresh pre-change baseline. Static update partition, joint
separation, reset pose/geometry and the existing animated base anchor plus cloth
checks pass. Maximum reset pose error1.95e-7 and vertex component error7.16e-7.
Native and standalone CPU output match exactly. An initial fixture compile used
copy-list syntax with an explicit Bullet constructor; corrected before any
successful build. The first parity comparison used a different path separator
in its input; rerunning with the identical canonical input matched exactly.

These checks establish this adapter's mass-frame math and integration invariants,
not original HotS/Domino dynamics, source physical-material presets, overlapping
volume union mass, exact primitive inertia, source COM override semantics or live
ragdoll appearance/FPS. Bullet's per-shape inertia implementations remain in use.
Cloth solver parameters were not retuned. Consult STATUS.md and rigid-com native
evidence for the current installed build and visual-check status.

Evidence: verification/m3/rigid-com-{baseline,corpus,mass-properties,reference,
portable-parity,native-result}.json and corresponding build/probe logs.
