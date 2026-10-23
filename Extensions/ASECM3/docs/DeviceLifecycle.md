# M3 graphics-context lifecycle

## Ordinary native reset

Both native D3D9 backends clear CGxDevice.m_context when reset fails and set it
when reset succeeds. They manage registered native shaders, texture wrappers
and stream pools. CGxShader::Valid lazily recreates its API object when m_loaded
is clear. GxDDS retains every DDS byte and callback address, allowing native
texture recreation to request the original subresources again. M3 private
streams use the native pool/buffer lists and follow their recreation path.

The module must not destroy/reparse models or reseed cloth for an ordinary
graphics reset. CPU scene, animation, physics and texture payload ownership
remain independent from those native API objects.

## Deferring unavailable draws

UnitWorldBinding checks the native context before loading a draw's next texture,
reading lighting, or creating a render guard. An unavailable context increments
deviceDeferrals and logs device_wait once. A later successful draw logs
device_resumed once; skipped calls do not increment draw/phase counters. A
binding's model/source/error and animation state are retained.

WorldInstance::Draw also implements this check for direct adapter callers.
It returns true with DrawDeferred()==true when the context is unavailable.
No submission, guard, stream, shader or native-state operation occurs then.
DrawDeferred is cleared at the beginning of every call. Existing restoration
failures still return false, including while the context is unavailable; they
are not cleared by deferral or recovery.

The asset-viewer frame similarly returns before its isolation boundary while
m_context is zero. Its existing render-boundary failure latch remains intact.
GxSurface clears an old shader-creation error only after all owned shaders
validate successfully on the same native device. It does not treat a different
device pointer as compatible or clear a currently failing shader.

## Boundaries and required validation

This handles a known unavailable native context. It does not classify every
D3D HRESULT, intercept/reset the device itself, or recover from an actual
render-state restoration failure. Full CGxDevice replacement still requires
destroying module device-owned objects before the original device teardown,
as specified in PORTING.md. Retained CPU source data is not proof that stale
device pointers can be used after that lifetime boundary.

Compilation and the normal pre-login regression do not exercise loss/reset.
Window resize/fullscreen/focus transitions with M3 visible still need an actual
native runtime check. Look for device_wait/device_resumed when the unavailable
state is observed; an immediate successful native reset may produce neither.
Verify continued textured submissions, ordinary M2 rendering, preserved model
and animation state, zero restoration failures and clean shutdown afterward.
