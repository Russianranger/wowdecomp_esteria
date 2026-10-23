# Primitive particle emission

The portable `PrimitiveEmission` object adds source Point (0), Sphere (2) and
Disc (5) shapes with Radial (1) and Random (3) directions to camera-facing
billboard particles (type 0). The existing Mesh (7) / Mesh Normal (4) lane stays
separate and retains source region/red-density sampling.

Source enum and field evidence: the pinned producer `bl_enum.py`,
`m3_particles.py`, `io_shared.py`, `bl_graphics_draw.py`, and the module's pinned
`schema/structures.xml`. The producer describes sphere/disc particles as emitted
within the shape, uses an explicit radius, and draws the disc in its local XY
plane. Independent historical field evidence is available at
https://github.com/flo/m3addon/blob/master/structures.xml (PAR_ emission area,
cutout radius, emission type and particle type fields). It is not evidence of
runtime/source-engine distribution parity.

## Sampling

- Point emits at its authored bone origin. Radial at that centre uses a uniform
  spherical direction, as does Random; the source engine's degenerate-origin
  policy has not been independently established.
- Sphere samples uniformly within a volume using a uniform spherical direction
  and cube-root radius. Disc samples uniformly in local XY using square-root
  radius. Radial direction follows the sampled radial vector; Random draws an
  independent uniform spherical direction.
- A source `flags & 0x10` cutout uses the animated inner radius: cube-root shell
  sampling for spheres and square-root annular sampling for discs. Reversed,
  negative, non-finite or over-budget radii report an error. A zero-volume sphere,
  zero-area disc or equal inner/outer radii yields no birth, not an origin fallback.
- Radius animation is sampled from the selected effect clip at the birth's source
  millisecond. The caller supplies deterministic RNG state, as for mesh emission.
- Native preview/world updates interpolate two observed solved bone transforms at
  each accepted birth, alongside the existing observed actor transform interval.
  Translation/uniform scale interpolate linearly and rotation takes the shortest
  quaternion arc. Skew, nonuniform/collapsed scale and handedness changes remain
  explicit unsupported cases. No physics rewind or exact solver-substep history
  is claimed. Direct portable `Advance` uses its supplied current frame.

All sampling remains module-owned. World/local particle storage, drag/gravity,
appearance capture, lifetime/phase clocks and capacity budgets retain the existing
implementation. Bone sampling does not count as a mesh-surface sample. Errors now
name the source emitter index. Preview material evaluation is private to each
emitter so two emitters sharing one material do not add duplicate overrides to
the same evaluation context.

## Remaining work and evidence

Plane, Cube, Cylinder and Spline emission, Constant/Z-Axis/Mesh directions on
primitives, source-specific emission distributions and non-billboard particle
orientation remain incomplete. In particular, producer dimension conventions
need further validation before implementing the rectangular/cylindrical shapes.
Copies, ribbons, external forces, collisions and HotS actor activation rules are
not implemented by this addition.

Native core/client/AssetViewer and standalone core compilation are the current
evidence. No tests were added or run. No representative primitive-emitter native
rendering, source-engine parity, animated-transform/FPS acceptance or complete M3
support is claimed. The full client now includes this addition; live validation remains pending.
