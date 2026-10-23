# Embedded M3 effect timelines

Preview and world loaders now request embedded effect setup when no explicit
`.effectbank` input or declaration error exists. A successfully parsed explicit
bank retains priority. An unreadable or rejected declaration does not silently
fall back to the embedded timeline.

The portable entry points accept an opt-in `embeddedEffects` argument:
`Scene::Parse(bytes, true)` and `Scene::FromDocument(document, true)`. Default
false preserves existing callers which explicitly bind a companion later.
Setup happens during the original decode; it does not copy/reparse a complete
Scene, create a self-owned shared pointer, read another file or run on rendering
callbacks. Setup failure is isolated in `EffectSetupError()` and reported by
both host loaders through their existing separate effect error channel.

## Ownership and independent clocks

An embedded lane needs particle owners and at least one uniquely matched local
SEQ/STG clip. Its particle links use the exact same Document record indices and
bone indices. Duplicate names do not imply ambiguity between two records that
are already identified in one document. Owner count is capped at 4096 and bone
indices must fit the source skeleton. Existing per-phase particle/key/live
capacity limits and supported emitter-shape checks remain in force.

Original model sequences and their local group indices are retained separately.
Binding RequiredAnims can replace the body clip catalog, but the embedded effect
catalog and local AnimationBank remain unchanged. All effect scalar, burst-count,
material and clip sampling routes through the selected effect bank. Its source is
reported as `<embedded>`; the existing model source identifies the owning file.

Material overlays use same-document root/layer/composite locations and typed
reference IDs. They do not match records by material name. Unsupported integer
overlays retain the existing isolated material-error behaviour. No material
names, bone names or animation IDs are translated to another file.

## Activation and limits

This exposes authored local effect clips to existing M3 effects controls. It does
not activate particles automatically, assign a local effect clip to a WoW emote,
or replace the missing HotS actor/catalog activation rules. The currently
implemented mesh-normal billboard emitter lane is unchanged; other emission
shapes, ribbons and copies are not made renderable by this timeline addition.
Models with no local clips do not obtain effect clips from an unrelated body bank.

Native core/client/AssetViewer and standalone portable core compile. No tests
were added or run. The full client is linked and installed; process startup and package hashes
were read back. No live embedded-emitter appearance, animation-bank
coexistence, device lifecycle or FPS acceptance is claimed.
