# Skyriding: retail asset map

Everything the retail Skyriding look needs, mapped to FileDataIDs (FDIDs) of retail **12.1.5.70575**. Mapped
2026-10-25; nothing here is installed yet. Status of the work: `SKYRIDING_CHECKPOINT.md`.

## Sources

| Source | Used for |
|---|---|
| `community-listfile-withcapitals.csv`, github.com/wowdev/wow-listfile release 202610241710 | FDID -> path |
| wago.tools DB2 CSV exports (SpellName, SpellXSpellVisual, SpellVisualEvent, SpellVisualKit(Effect/ModelAttach), SpellVisualEffectName, ScreenEffect, Positioner*, UiTextureAtlas(Member), Mount, MountXDisplay, CreatureDisplayInfo, CreatureModelData) | spell -> visual -> model chains, atlas coordinates, mount models |
| `Support Files/animation-names.csv`, github.com/Frostshake/WMVx | animation ID -> name |
| CDN through `RetailAssetProbe` | the files themselves, and M2 header inspection |

**CDN config.** The client's `Data/Retail/RetailCasc.ini` pins an older build without a `cdn=` key, so it can only
read files already in its cache (uncached reads fail with CascLib 6002). A config pinned to the current build reads
them (`build=d718abcc42604d8efbd35aecfa5bb75b`, `cdn=33ab93e2ba5f5ced1b32d7f92a939ed5`,
`version=12.1.5.70575`, `product=wow`). Ship the files below as an offline pack, or add a current-build config.
DB2 files (e.g. 897532) still failed to read from the CDN; the wago CSVs stand in for them.

## Ability icons (`Interface/ICONS/`)

All BLP2, DXT5, 64x64: the format the client already loads for Retail mount icons.

| Ability | Icon | FDID |
|---|---|---|
| Lift Off | `Ability_DragonRiding_Launch01` | 4640492 |
| Surge Forward | `Ability_DragonRiding_ForwardFlap01` | 4640490 |
| Skyward Ascent | `Ability_DragonRiding_UpwardFlap01` | 4640498 |
| Whirling Surge | `Ability_DragonRiding_BarrelRoll01` | 4640477 |
| Aerial Halt | `Ability_DragonRiding_HaltTheWinds01` | 5003205 |
| Flap / glide | `Ability_DragonRiding_DragonridingGliding01` | 4640486 |
| Vigor | `Ability_DragonRiding_Vigor01` | 4640499 |
| Thrill of the Skies | `Ability_DragonRiding_ThrilloftheSkies01` | 4640497 |
| Ground Skimming / diving | `Ability_DragonRiding_Diving01` | 4640481 |
| Switch flight style | `Ability_DragonRiding_SwapFlightStyles01` | 5145511 |
| Dynamic / steady flight | `Ability_DragonRiding_DynamicFlight01` / `StaticFlight01` | 5142725 / 5142726 |
| Skyriding (UI) | `UI_skyriding` | 5922053 |
| Bronze Rewind (Timelock) | `Ability_DragonRiding_BronzeRewind01` | 4640479 |

Retail spells (for reference): Surge Forward 372608, Skyward Ascent 372610, Whirling Surge 361584, Aerial Halt 403092,
Lift Off 404191, Thrill of the Skies 377234, Ground Skimming 404183, Skyriding Charges 383359, Switch Flight Style
436854, Flight Style: Skyriding 404464.

## HUD textures and atlas regions

Coordinates are pixels in the texture (left,top-right,bottom); `SetTexCoord` = pixel / texture size.

| Texture | FDID | Size | Format |
|---|---|---|---|
| `Interface/Widgets/DragonridingVigorWidgets.BLP` | 4730866 | 512x512 | BLP2 uncompressed ARGB |
| `Interface/Widgets/DragonridingVigorWidgetMask.BLP` | 4734089 | 128x128 | |
| `Interface/Widgets/DragonridingSGVigorWidget2x.BLP` (silver/gold/bronze pips) | 5334458 | 2048x1024 | |
| `Interface/Widgets/DragonridingSGVigorWidgetMask.BLP` | 5334461 | 128x128 | |
| `Interface/HUD/UIActionBar.BLP` | 4613342 | 256x1024 | |
| `Interface/HUD/UIActionBar2x.BLP` | 4615764 | 512x2048 | |
| `Interface/HUD/UIActionBarIconFrameMask.BLP` | 4626072 | 64x64 | |
| `Interface/HUD/UIActionBarBackground.BLP` | 4615740 | 8x8 | |
| `Interface/Widgets/DragonridingWidgets.BLP` (reticle, race position) | 4697252 | 512x64 | |

| Atlas member | FDID | Region | Texture | Size |
|---|---|---|---|---|
| `dragonriding_vigor_background-2x` | 4730866 | 373,120-434,191 | 512x512 | 61x71 |
| `dragonriding_vigor_decor-2x` | 4730866 | 299,1-392,118 | 512x512 | 93x117 |
| `dragonriding_vigor_fill-2x` | 4730866 | 394,1-466,73 | 512x512 | 72x72 |
| `dragonriding_vigor_fill_flipbook-2x` | 4730866 | 1,1-297,371 | 512x512 | 296x370 |
| `dragonriding_vigor_fillfull-2x` | 4730866 | 299,120-371,192 | 512x512 | 72x72 |
| `dragonriding_vigor_flash-2x` | 4730866 | 1,373-121,503 | 512x512 | 120x130 |
| `dragonriding_vigor_frame-2x` | 4730866 | 123,373-243,503 | 512x512 | 120x130 |
| `dragonriding_vigor_mask-2x` | 4734089 | 0,0-128,128 | 128x128 | 128x128 |
| `dragonriding_vigor_spark-2x` | 4730866 | 394,75-487,95 | 512x512 | 93x20 |
| `dragonriding-sgvigor-background-2x` | 5334458 | 1200,656-1271,755 | 2048x1024 | 71x99 |
| `dragonriding-sgvigor-fillfull-2x` | 5334458 | 1200,554-1272,654 | 2048x1024 | 72x100 |
| `dragonriding-sgvigor-fill-flipbook-2x` | 5334458 | 803,427-1091,927 | 2048x1024 | 288x500 |
| `dragonriding-sgvigor-flash-2x` | 5334458 | 1093,427-1198,567 | 2048x1024 | 105x140 |
| `dragonriding-sgvigor-frame-silver-2x` | 5334458 | 1200,427-1289,552 | 2048x1024 | 89x125 |
| `dragonriding-sgvigor-frame-gold-2x` | 5334458 | 1093,823-1182,948 | 2048x1024 | 89x125 |
| `dragonriding-sgvigor-frame-bronze-2x` | 5334458 | 1093,569-1182,694 | 2048x1024 | 89x125 |
| `dragonriding-sgvigor-decor-silver-2x` | 5334458 | 632,803-749,954 | 2048x1024 | 117x151 |
| `dragonriding-sgvigor-decor-gold-2x` | 5334458 | 513,803-630,954 | 2048x1024 | 117x151 |
| `dragonriding-sgvigor-spark-2x` | 5334458 | 275,956-368,976 | 2048x1024 | 93x20 |
| `ui-hud-actionbar-iconframe` | 4613342 | 181,254-227,299 | 256x1024 | 46x45 |
| `ui-hud-actionbar-iconframe-slot` | 4613342 | 181,136-245,198 | 256x1024 | 64x62 |
| `ui-hud-actionbar-iconframe-background` | 4613342 | 181,411-227,456 | 256x1024 | 46x45 |
| `ui-hud-actionbar-iconframe-border` | 4613342 | 181,474-227,519 | 256x1024 | 46x45 |
| `ui-hud-actionbar-iconframe-down` | 4613342 | 181,521-227,566 | 256x1024 | 46x45 |
| `ui-hud-actionbar-iconframe-flash` | 4613342 | 181,568-227,613 | 256x1024 | 46x45 |
| `ui-hud-actionbar-iconframe-mouseover` | 4613342 | 181,643-227,688 | 256x1024 | 46x45 |
| `ui-hud-actionbar-iconframe-mask` | 4626072 | 0,0-64,64 | 64x64 | 64x64 |
| `ui-hud-actionbar-gryphon-left` / `-right` | 4613342 | 1,136-179,303 / 1,305-179,472 | 256x1024 | 178x167 |
| `ui-hud-actionbar-wyvern-left` / `-right` | 4613342 | 1,474-179,641 / 1,643-179,810 | 256x1024 | 178x167 |

The flipbooks are animation strips (fill / burst / lightning frames); their frame grid is not in these tables and has
to be read off the texture. The 3.3.5 client has no texture masks: round icon corners and the pip mask need either
pre-masked textures or a mask feature.

## Wind / speed effect

Retail plays visual kit **159809** from the hidden spell *Dragonriding: AdvFlyDown (+AdvFlyDownEnd) + Speed Aura*
(379415, also 373404 and the mounted 432158) while diving / flying fast:

| Model | FDID | Placement (SpellVisualKitModelAttach) |
|---|---|---|
| `spells/FX_Speed4_Aura.m2` | 4579907 | positioner 1797, scale 1.25, anim 158 -> end 159 |
| `spells/FX_Speed4_Aura01.m2` | 4579908 | positioner 1797, scale 1.25, anim 214 -> end 159 |
| `spells/FX_Speed4_Aura02.m2` | 4640283 | four copies, positioners 1841/1842/1846/1847: +-20 deg yaw, -5 deg pitch, two rolled 180 deg and raised 0.16 |

Textures: `spells/FX_Speed4_Aura_4580999.blp`, `spells/FX_Speed4_Aura_4640906.blp`, plus `00`/`lod` skins
(4580537, 4581003, 4640284). The kit also has screen effect 1918 (a light/ambience override; no full-screen
texture) and two sound kits. The engine's layout check passes `FX_Speed4_Aura.m2`.

Not decoded: the positioners' source points (`SrcVal` 15/16/19, 60-63) are engine enums. The layout (a ring of streak
models fanned around the rider) suggests rider- or camera-relative placement; confirm by eye once attached.

Other kits: Surge Forward / Whirling Surge / Aerial Halt `spells/FX_SurgeForward_Impact.m2` (4579917, attachment 17);
Skyward Ascent / Lift Off `spells/10FX_HallsofInfusion_PrimalTsunami_InfusedGlobule_Cast.m2` (4290528); flight style
switch `world/expansion09/doodads/fx/10fx_generic_wind_precasthand_high.m2` (4208667) and
`spells/10FX_VOTI_Dathea_ZephyrSlam_Cast.m2` (4208670). Dive sound loops: `Sound/Spell/DragonridingAdvFlyDown...`
(4684310/12/14).

## Mount animations

Retail skyriding uses its own animation IDs (`AdvFly*`; names from WMVx `animation-names.csv`):

| ID | Name | ID | Name |
|---|---|---|---|
| 1618 | AdvFlyStart | 1620 / 1622 | AdvFlyLand / AdvFlyLandRun |
| 1628 | AdvFlyIdle | 1524 | AdvFlyForward |
| 1532 | AdvFlyForwardGlide | 1722 / 1724 | AdvFlyForwardGlideSlow / Fast |
| 1528 / 1530 | AdvFlyUp / AdvFlyDown | 1678 / 1588 | AdvFlyDownStart / AdvFlyDownEnd |
| 1520 / 1522 | AdvFlyLeft / AdvFlyRight | 1534 / 1556 / 1558 | AdvFlyRoll / RollStart / RollEnd |
| 1630 / 1634 | AdvFlyRollRight / AdvFlyRollLeft | 1638 / 1680 | AdvFlyFlap / AdvFlyFlapBig |
| 1702 / 1726 | AdvFlyFlapUp / AdvFlySecondFlapUp | 1706 | AdvFlyFlapFoward (sic) |
| 1704 | AdvFlySlowFall | 1694 / 1696 | Glide / GlideEnd |

All are above the 3.3.5 AnimationData range, so the client's fallback table has no entries for them; selection must go
by ID on Retail models.

## Skyriding mounts (Mount type 402)

| Mount | Mount ID | Display | Model FDID / path | Size | Seqs | AdvFly | Layout check |
|---|---|---|---|---|---|---|---|
| Highland Drake | 1563 | 106003 | 4227968 `character/companiondrake/companiondrake.m2` | 23 MB | 97 | 19 | passes |
| Renewed Proto-Drake | 1589 | 107058 | 4278602 `character/companionprotodragon/companionprotodragon.m2` | 8 MB | 137 | 17 | passes |
| Windborne Velocidrake | 1590 | 105268 | 4281540 `character/companionpterrodax/companionpterrodax.m2` | 8 MB | 108 | 17 | passes |
| Cliffside Wylderdrake | 1591 | 107056 | 4252337 `character/companionwyvern/companionwyvern.m2` | 6 MB | 104 | 18 | **fails: "M2 lookup index is out of range"** |
| Winding Slitherdrake | 1588 | 105540 | 4252339 `character/companionserpent/companionserpent.m2` | 12 MB | 78 | 17 | passes |
| Grotto Netherwing Drake | 1744 | 112642 | 5143343 `character/CompanionNetherwingDrake/...m2` | 11 MB | 97 | 19 | passes |
| Algarian Stormrider | 1792 | 113958 | 5305977 `character/RostrumStormGryphon/RostrumStormGryphon.m2` | 11 MB | 98 | 12 | passes |
| Flourishing Whimsydrake | 1830 | 114211 | 5228774 `character/RostrumFaerieDragon/...m2` | | | | not fetched |
| Others | | | Auspicious Arborwyrm 4788406, Anu'relos 5154325, Polly Roger / Hooktalon 5568566, Delver's airships 5486691 | | | | not fetched |

**None of these use a shared skeleton (no `SKID` chunk)**, so the "shared SKELs unsupported" limit in
`retail-native-mounts.md` does not block them. The Highland Drake has 3 external `.anim` files (AFID).
`CreatureDisplayInfo.MountMaxBankingAngle` is 45 deg for all of them.

These models are customizable "companion" drakes: their look comes from the drake customization system (barbershop
`Interface/Barbershop/DragonRidingCustomization.BLP`), not fixed display textures. Picking one default look (geosets +
textures) per drake is part of making a mount pack.

## What this means for the three asks

1. **HUD:** every icon and widget texture is mapped and in formats the client loads. Work: alias the FDIDs to
   `Interface\...` paths, then rebuild `Addons/EsteriaSkyriding` with the vigor pips, action-bar icon frames and
   ability icons. Masks need pre-masked art.
2. **Wind:** `FX_Speed4_Aura*.m2` attached to the rider or camera, faded in by speed. Needs a client hook to own a
   world model that follows the camera/rider; the placement numbers above are the starting point.
3. **Mounts:** the drakes above are usable through the existing native-mount pipeline (`native_mount.py`). The
   controller then plays AdvFly animations by state (glide / dive / flap / roll / land) on models that have them, and
   falls back to stock fly animations on WotLK mounts.
