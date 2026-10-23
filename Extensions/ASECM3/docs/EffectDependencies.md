# Explicit companion effect dependencies

## Declaration

A model may have an adjacent file named exactly <model.m3>.effectbank. Its
ASCII header and single adjacent M3A basename declare an auxiliary effects bank:

```text
ASECM3_EFFECT_BANK_V1
storm_effect_deathragdolldissipate.m3a
```

LF/CRLF are accepted. The declaration is limited to1024bytes and one basename
of at most255bytes, ending in .m3a. Paths, extra lines, control/NUL characters,
unknown headers and other extensions are rejected. The bank is resolved beside
the model, for both absolute local paths and virtual MPQ paths. This is an
explicit companion-input format for the module; it is not part of Blizzard's
M3 file format or a guess that every similarly named model needs this bank.

The companion supports models whose dependencies originate in external actor/
model catalogs. It does not require reading an entire HotS catalog at runtime,
change the source M3, run scripts or activate a particle phase automatically.
It currently declares one bank; multiple simultaneous auxiliary banks are not
implemented. The separate effect-owner compatibility checks still apply.

## Loader integration (compiled source candidate)

World SourceJob and preview AssetFrame snapshot the optional declaration and
bank through their existing SFile provider on the client thread. Portable
parsing/owner binding occurs on their workers after skeletal-bank handling.
The effect snapshot has a separate role and is never offered to the skeletal
bank binder. Effect snapshot sizes are checked against remaining model/job
budgets before allocation. Preview reads also now reject a nonzero high file
size word instead of accepting a truncated32bit size.

Absent declarations now permit the model's own embedded effect timeline; see
[EmbeddedEffects.md](EmbeddedEffects.md). A present but unreadable,
malformed or incompatible declaration/bank leaves the mesh/body bank available,
records a separate effect error and reports it once through the existing
bounded built-in logger. Snapshot bytes are released after worker completion.
The world ready flag publishes effect metadata/error together with the source
scene; render callbacks do not read worker-owned strings before that publication.

Preview GetAssetInfo exposes effectBankSource, effectSequences, effectBankError
and effectsRendering=false. World UnitWorldInfo exposes its separate source,
sequence count and error, and world traces include effectBank/effectError. These
fields describe a bound dependency; they do not claim particle rendering.

## Staged supplied-file profiles

deploy/ASECM3-EffectDependencies-20261016 contains four explicit declarations and
four copies of the68032bytebank, beside the corresponding humanoid death-model
filenames. Its manifest records original mesh hashes and staged input hashes.
No model bytes were copied or edited. The inputs were derived from the local
source dependency documented in EffectAnimationBanks.md and passed the strict
owner/signature checks on all four models. They are not installed and are
outside the portable module/runtime export tree.

ASECM3Inspect <model.m3> --effect-declaration <model.m3.effectbank> can inspect
a staged declaration separately from the original model. It resolves the bank
beside the declaration's owner filename and runs the same explicit core binding
and scalar sampling inspection. All four outputs equal the explicit-bank
baseline, including unchanged body ownership/bind pose and56rate samples.
The standalone core matches all four. Nine malformed declarations are rejected.

Evidence:effects-dependency-{declaration-inspection,portable-parity}.json,
effects-dependency-{final-build,portable-build}.log and the staged manifest.
These cover the core declaration/path/binding contract and compiled native
adapters. They do not exercise actual native SFile worker loading, missing-bank
recovery in the UI, live particles or actor activation. No native EXE was built
or deployed in this step. The installed CD258138client/sourcekitv5remain the
prior runtime snapshot; the current source candidate includes these changes.
