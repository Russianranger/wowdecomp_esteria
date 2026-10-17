# First native Retail weapon importer

Implemented scope: a resolved, supported Retail weapon model/texture becomes a new
Esteria item entry and display, backed by original assets in the client's Data folder.
This backend does not yet resolve human item/set names against Retail DB2 metadata.
Mounts, armor sets, original icons and Retail gameplay scaling remain later milestones.

## Build

Use the Retail-enabled client configuration described in [retail-casc.md](retail-casc.md).
Build `WowClient`, `RetailAssetAcquire`, `RetailAssetProbe` and `RetailAssetTests`.
The Python tools require Python 3.10+, Docker access and the current Esteria source
checkout. Client-table inspection reuses that checkout's StormLib helper and its
configured x64 DLL. Missing or unreadable providers stop the import.

No server compilation is needed for this weapon backend. Its schema is checked
against the actual running `ac-database` world database. Credentials are consumed
inside that container and are not exported into the pack or command output.

## Stage a weapon

Close the client before scanning/installing: archive providers may be locked.
Supply an online/local CASC config with the explicit source build pinned.

```powershell
python Tools/RetailAssets/native_import.py stage-weapon `
  --client 'C:/WoW-Dev' --server-root 'C:/sources/EsteriaWoW' `
  --source-config 'C:/config/RetailCasc.ini' `
  --acquire build/retail-assets/Release/RetailAssetAcquire.exe `
  --stage build/imports/arakkoa-blue-01 --slug arakkoa-blue `
  --name 'Arakkoa Sword - Native CDN' --model 916541 --texture 916543
```

The stage must be new. A later stage for the same source graph reuses its allocated
IDs from `Data/Retail/allocations.sqlite`. IDs are reserved against every relevant
root/locale MPQ provider, loose client tables, installed packs, server base and
continuation files, live SQL overlays, item templates and prior ledger allocations.
Existing archives are read only and remain unchanged. A reservation is not a claim
that installation or live rendering succeeded.

`RetailAssetAcquire` resolves the supported model's skin, fixed textures and external
animations, plus the chosen object-skin texture. It validates temporary memory copies
using the existing adapter and writes only original decoded CASC bytes as `<ID>.bin`.
`manifest.json` records SHA-256 hashes, sizes, the source build, target definitions,
baseline fingerprints and explicit limitations. The source CASC cache remains in
the acquisition config's cache location; the client pack contains original decoded
assets, rather than an exported legacy model or an entire encoded CASC installation.

The initial weapon policy is deliberately explicit: a rare, level-one one-handed
sword with 1-2 damage and 2000 ms delay, with a legacy fallback icon. Its name and
stats are authored test metadata, not claimed Retail item-table metadata. The manifest
records `retail_item_id: null` until an authoritative item resolver is implemented.

## Validate and install

```powershell
python -m unittest discover -s Tools/RetailAssets -p test_native_import.py -v
python Tools/RetailAssets/check_server_install.py build/imports/arakkoa-blue-01
python Tools/RetailAssets/native_import.py install `
  --client 'C:/WoW-Dev' --server-root 'C:/sources/EsteriaWoW' `
  --stage build/imports/arakkoa-blue-01
```

The MySQL check uses uniquely named disposable test tables cloned from the actual
schema and removes only those tables afterward. It tests installation, repeated
installation and rollback of partial inserts on a conflict. No game content rows
are changed by that check.

Installation creates a new item template, Item record, ItemDisplayInfo record and
ownership entry in `esteria_retail_imports`. An advisory lock serializes participating
installers; transaction guards abort conflicting or changed rows. Repeated installation
verifies the same definitions rather than deleting or replacing existing content.
The generated SQL intentionally uses conditional inserts and no content deletes.
It is retained in the pack as `server.sql` for review and reproduction.

This deployment tool applies only its generated SQL; it does not invoke the server
updater or build/restart services. Recheck the stage before installation and arrange
a scoped worldserver reload/restart when ready to activate the new DBC overlays.

Client installation produces:

```text
Data/Retail/
  allocations.sqlite
  installed.json
  Assets/<product>-<build>/<FileDataID>.bin
  Packs/<slug>/{manifest.json,server.sql,receipt.json,originals/,tables/,equipment.csv}
  Registries/<revision>/{item.dbc,itemdisplayinfo.dbc,equipment.csv}
  RetailCasc.ini
```

The generated config uses `mode=files`. All model dependencies come from the local
original-byte store; gameplay makes no CDN requests. The current registry accepts
multiple packs from one pinned product/build and refuses mixed builds or duplicate
target records. Acquisition hashes are verified before installation and again by
the verification command; runtime format validation remains enabled.

The client appends the registry's new WDBC records to its actual resolved tables in
memory, adjusting only the new metadata's string offsets. Existing rows and string
data remain verbatim, including historical optional metadata; new records are
strictly checked for invalid strings, duplicate IDs and conflicts. This makes the
new records visible through normal database indexing, enumeration and compressed
display lookup. The equipment map contains only the newly allocated display IDs.
The broad mainhand smoke-test override is disabled by the normal launcher.

## Launch and accept

Place the new executable as `WowClientRetailNative.exe` beside the client's Data
folder. Copy `Tools/RetailAssets/LaunchRetailNative.cmd` beside it, or run:

```powershell
./Tools/RetailAssets/Launch.ps1 -ClientDirectory 'C:/WoW-Dev' -VerifyItem 1000000
python Tools/RetailAssets/verify_install.py --client 'C:/WoW-Dev' --slug arakkoa-blue
```

Use the actual allocated ID from the manifest if it differs. The launcher sets the
pack configuration for that child process and clears both preview overrides.
The server must have reloaded its item/display SQL overlays before creating the item.
An authorized GM can then use `.additem <allocated-item-id>` and equip the new item.
Check its name/tooltip, ordinary inventory identity, mainhand/offhand behavior,
movement, draw/sheath, relog and observation from another provisioned client.

The verification tool records original-file hashes, SQL readback, server readiness
and table-mount logs separately from live acceptance. It never marks an item live
accepted based on the old floating preview or on table registration alone.

Development verification on 2026-10-17 installed the blue Arakkoa weapon as item
`1000000`, display `160000`, with four original files from build
`88d5e8ef05ec5a8446cdec93d142d7c6`. The standalone offline model read, native
client's Item/ItemDisplayInfo mounts, SQL readback, repeated installation and scoped
worldserver restart were verified. C++ parser/table tests, Python importer tests
and the isolated MySQL collision/rollback checks passed. Obtaining/equipping this
new item and its in-world behavior still require live acceptance; the earlier
mainhand override proof is separate evidence.

Rollback is not automated yet. Do not delete learned/owned content or recycle IDs
while player items reference them. The original client archives and executable are
not patched by the importer; preserve the matched pack/SQL ownership receipts for
any later scoped retirement.
