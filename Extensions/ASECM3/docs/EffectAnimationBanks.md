# Auxiliary effect animation banks (source candidate)

## Authored dependency found

The local HotS modeldata.xml defines HeroDeathRagdoll.RequiredAnims as
Assets/Effects/Storm_Effect_DeathRagdollDissipate/Storm_Effect_DeathRagdollDissipate.m3a.
The inspected Alexstrasza actor derives from StormHeroDeathRagdoll. That actor
explicitly schedules Dissipate three seconds after its Death animation ends,
plays Dead while waiting, and terminates on Dissipate completion. This is actor
context, not a timer to apply indiscriminately whenever a viewer loads a mesh.
The full applied skin/model catalog inheritance has not been resolved here.

The exact bank and Alexstrasza metadata were read from local HotS CASC into
verification/m3/effects-source-dependencies, with hashes in manifest.json.
The first restricted-session storage open failed with WinError2; the same exact
local storage opened on the interactive host. No HotS executable was launched.
The bank is68032bytes,SHA2567520655021d54813fd27bd0213e13b484f35df92dcc6af85804634fd9306aefb.
These extracted game assets are research inputs outside the module/export tree.

The bank contains5bones,2particle owners and a3333msDissipate clip. Authored
emission-rate curves reach93.808708particles/s for ashes and50.550980for smoke.
The supplied humanoid death meshes have zero default emission and no local
rate/count animation channels. Omitting the shared bank therefore loses the
authored emission curves; adding a guessed constant rate would not recover them.

## Separate ownership lane

EffectBankBinding.cpp is a separate portable core object. For an explicitly
declared dependency, it matches each particle owner using its named source bone
and complete field-to-animation-ID signature, with matching PAR type/version.
Absent/ambiguous owners and many-bank-owners-to-one-mesh matches are rejected.
Reference interpolation policies must agree for actual decoded incoming tracks.
Inactive shared-bank defaults commonly use constant interpolation while mesh
defaults retain linear references, so those inactive policies are not required
to be equal. Unanimated fields retain mesh defaults.

Scene::WithEffectAnimationBank stores this dependency separately from the
skeletal bank. It does not replace model sequences, apply the five-bone bank as
the body skeleton, merge material IDs or activate an actor. EffectSequences and
EffectSequenceGroup expose the dependency's own clip/group domain. The
SampleEffectScalar convenience API samples only bound animated float fields,
using the corresponding source group/time and the existing typed evaluator.
It does not implement integer burst counts or cache a runtime particle clock.

Compatibility checking is not discovery authority: a caller still needs a
declared dependency, not an arbitrary file whose IDs happen to overlap.
The current native loader does not yet discover/attach this auxiliary lane.
Material-bank ownership/merging, actor activation and particle emission/drawing
also remain incomplete. The installed client is unchanged by this source step.

## Evidence and validation scope

ASECM3Inspect <mesh.m3> --effect-bank <bank.m3a> reports owner links, effect clips
and rate samples, and checks that skeletal sequence count, skeleton size,
skeletal-bank source and all bind-pose world matrices remain unchanged.
All4humanoid death variants bind both owners; their56sampled rate values match
independent sampling of the source bank curves. A base mesh without matching
owners is rejected. The standalone module build returns exactly the same
4model outputs. Existing18mesh cloth output remains exactly unchanged.

This does not prove animation at every body timestamp, particle rendering,
source actor parity or live FPS. No auxiliary-bank bytes were installed in the
client or bundled in the portable module/runtime package.

Evidence:effects-bank-binding-parity.json,effects-bank-portable-parity.json,
effects-bank-cloth-regression.json,effects-shared-bank-native.json,
effects-source-dependencies/{manifest,shared-bank-emission-controls}.json and
effects-bank-{policy-build,portable-build,cloth-build}.log under verification/m3.
