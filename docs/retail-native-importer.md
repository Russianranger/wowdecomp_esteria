# Native Retail content imports for Esteria

Status: architecture with a first implemented weapon backend. See the
[current import workflow](retail-import-workflow.md) for original-byte acquisition,
persistent IDs, additive client records and scoped server installation. Automatic
Retail metadata discovery, mounts, armor and the general native format pipeline
described below remain future work. The current backend uses explicit resolved
file IDs and authored test gameplay metadata.
The user accepted the native hand attachment proof in character selection on 2026-10-17.

## Intended contract

A request such as "add the latest warrior tier sets" or "add Retail mount X" should
produce new, independently usable Esteria content, with its complete client asset
graph and matching server definitions. These examples specify the desired workflow;
they are not instructions to import a particular asset in this design change.

- Preserve Blizzard's downloaded asset bytes. Do not export a converted M2, skin,
  skeleton, animation, texture or effect, and do not package a legacy substitute.
- Implement missing format/rendering behavior in the client. Unsupported assets
  must produce a concrete capability report, not silently fall back to conversion.
- Allocate new target IDs. Preserve existing items, displays, spells and creatures
  unless the request explicitly identifies an intended replacement.
- Produce real server items and mount definitions, with client records that agree
  with them. A display override alone does not meet the contract.
- Pin and record the source product/build and resolved variants. Repeating the
  same import must not allocate duplicate content or change an installed build.
- Keep downloaded game data, keys, database credentials and caches outside Git.

"Unmodified assets" does not mean identical Retail and Esteria database records.
New Esteria records necessarily contain target IDs and target gameplay definitions.
Retail gameplay systems cannot be assumed to exist on the Esteria server.

## Request to installed content

1. Resolve the human request against source metadata for a pinned Retail build.
   Resolve item/set/mount identities, names, variants, appearances and dependencies.
   "Latest" is resolved at import time and then recorded as an explicit result.
   FileDataIDs and filenames alone are not an item or set catalog. Ambiguous scope
   such as recolors or difficulty variants must be made explicit in the import plan.
2. Build a dependency graph from the matching metadata and asset chunks. Include
   model/skin profiles, skeletons, animations, textures, icons and effects; include
   armor components, texture layers and race/sex choices when the appearance needs
   them. Record dependency roles instead of treating every model as a hand item.
3. Acquire and verify the original files through CASC. Preserve the encoded cache
   and the original decoded asset bytes as applicable, with their source identifiers
   and integrity information. Never write runtime-adapted buffers into this store.
4. Check the graph against native client capabilities before deploying it. Name
   missing features precisely and retain the acquired inputs for implementation.
5. Reserve target IDs against the actual current client and server baselines. Create
   the target records and a manifest linking those records to the original graph.
6. Stage a matched client/server pack, validate its references and install it with
   a receipt. Verify the installed records and perform the appropriate live checks.

## Client storage and identity

A proposed layout beneath the development client's Data directory is:

```text
Data/Retail/
  Cache/<product>-<build>/       # CASC-managed source cache
  Packs/<pack-name>/
    manifest.json               # source graph, target mappings and capabilities
    receipt.json                # installed hashes, versions and validation state
```

The storage design must preserve CascLib's expected cache layout and its ancestor
manifest rules; copying an arbitrary collection of files into Data is insufficient.
Installation paths are configurable and must not be embedded in a distributable pack.

The client mounts each pack's manifest as an asset source. Original asset references
must resolve within the pack's pinned build: a naked FileDataID is not a globally
immutable identity across builds. Multiple source builds require separate source
instances and build-qualified model/texture cache keys. The current process-wide
single-build source does not yet provide that capability.

Packs share downloaded dependencies when their build/content identity agrees.
They must be complete for offline use after acquisition, rather than depending on
a successful network request during equipping or mounting. Missing files are
reported before installation. Acquisition may use the CDN; normal gameplay should
read the verified local cache through the same native loader.

## Target records and server integration

The importer needs a persistent allocation ledger keyed by source build, source
record, variant and target namespace. Scan the effective client tables, current
server records and prior allocations before reserving anything. Use uniqueness
constraints or equivalent reservation checks, and retain IDs across reruns.
Never treat a Retail item ID, display ID or FileDataID as a free Esteria ID.

| Content | New target definitions |
| --- | --- |
| Item/weapon/armor | Server item entry, client item record, appearance/display record and native asset manifest links |
| Armor set | Independent item entries for its pieces, appropriate set metadata/gameplay definitions, and complete body/attachment appearance data |
| Mount | Creature/model/display definitions and a usable summon/learn path with matching client spell data and server behavior; optional acquisition item |

Generate records for the detected Esteria server schema and runtime, not a guessed
generic server. The server source/database location and effective client/server
table baselines still need discovery before implementing that adapter.

Client records can be added through merged target tables or a native supplemental
registry, but all relevant lookup paths must see the same new records. Supplemental
records are a proposed extension, not an existing database capability. Server item
queries, inventory/equipment, tooltips, inspection and appearance lookups must agree
on the new item/display identity.

Importing the original visual does not import Retail combat or progression code.
Item stats, set bonuses, procs, mount movement and requirements need an explicit
Esteria gameplay policy. Preserve source metadata for provenance; translate only
supported gameplay semantics and report any intentional difference. Do not present
an ordinary Esteria spell as an exact Retail mechanic without implementing it.

## Native client work still required

The current prototype reads original CASC data but prepares a legacy-compatible
buffer in memory, including model version/layout, blend and material adjustments.
It emits no converted art file. It does not yet implement a general native Retail
model/appearance pipeline or satisfy literal "no modification anywhere".

The long-term loader should parse the original supported formats into explicit
runtime structures, retaining their semantics and leaving source data immutable.
Extend the renderer/animation systems when the original features require it.

- Complete standalone item loading and supplemental item/display registration.
- Implement shared skeletons, newer animation layouts, materials and effects needed
  by the selected assets. The current adapter rejects several of these features.
- Implement creature/mount display resolution and normal rider/animation behavior.
- Implement armor texture composition, geosets, race/sex variants, attached geometry
  and associated visibility rules for Esteria's actual playable bodies.
- Add build-qualified source/cache identity, asynchronous acquisition and pack mounting.

Armor acceptance must use the existing custom race/body and appearance contracts.
Do not truncate custom race masks or substitute a stock body silently. Exact Retail
fit on a custom body is not guaranteed by downloading the original armor files.

## Deployment and acceptance

A pack contains original assets or verified cache references, its complete manifest,
the generated target records, schema-specific server migrations, validation results
and an installation receipt. Validate dependency closure, target-ID uniqueness and
client/server reference agreement before installation. Installing the same pack
again must be idempotent. Updates to a newer source build are explicit pack revisions.

Rollback removes only additions owned by that pack. Account for already-created
player items and learned mounts; do not blindly delete player data or recycle IDs.
Keep acquisition, registration, installed state and live acceptance separate.

For an item, acceptance includes obtaining the new item entry, tooltip/query data,
equipping and re-equipping, movement, combat/attachment behavior where applicable,
and another correctly provisioned client seeing the same item. Armor additionally
needs its supported race/sex and layering checks. Mounts need learning/summoning,
rider attachment, movement, relevant flying/swimming behavior and dismounting.
The accepted sword screenshot establishes character-selection hand rendering only.

## Implementation sequence

1. Build the manifest/ledger and source metadata resolver; add one supported weapon
   as a new client/server item with original assets and no existing-record overrides.
2. Add a mount with the native format features its complete graph requires and a
   real server summon/learn integration.
3. Add the armor appearance resolver and renderer features required for a complete
   selected tier set on the target bodies.
4. Expand named requests to complete sets/variant groups, repeatable pack updates
   and client distribution using the same validated import mechanism.

The converter workflows in the retroport skill are not the backend for this native
mode. Their target-baseline, identity and validation safeguards remain relevant.
