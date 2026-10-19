#include "Engine/Source/Retail/Appearance.h"
#include "Engine/Source/Retail/Db2.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <unordered_map>

namespace Retail::Db2::appearance
{
    namespace
    {
        void AppearanceLog(const char* format, ...)
        {
            va_list args;
            va_start(args, format);
            std::fprintf(stderr, "appearance: ");
            std::vfprintf(stderr, format, args);
            std::fputc('\n', stderr);
            va_end(args);
        }

        // --- table declarations ------------------------------------------------------------------
        constexpr Field kChrRaceXChrModelFields[] = {
            { "ChrRacesID" }, { "ChrModelID" }, { "Sex" }, { "AllowedTransmogSlots" },
        };
        constexpr Definition kChrRaceXChrModel{
            "chrracexchrmodel", "DBFilesClient\\chrracexchrmodel.db2", 0xA203BC29u,
            kChrRaceXChrModelFields,
        };

        constexpr Field kChrModelFields[] = {
            { "FaceCustomizationOffset", 3 }, { "CustomizeOffset", 3 }, { "ID" }, { "Sex" },
            { "DisplayID" }, { "CharComponentTextureLayoutID" }, { "Flags" },
            { "SkeletonFileDataID" }, { "ModelFallbackChrModelID" }, { "TextureFallbackChrModelID" },
            { "HelmVisFallbackChrModelID" }, { "CustomizeScale" }, { "CustomizeFacing" },
            { "CameraDistanceOffset" }, { "BarberShopCameraOffsetScale" },
            { "BarberShopCameraRotationFacing" }, { "BarberShopCameraRotationOffset" },
        };
        constexpr Definition kChrModel{
            "chrmodel", "DBFilesClient\\chrmodel.db2", 0x03FAB755u, kChrModelFields,
        };

        constexpr Field kChrCustomizationOptionFields[] = {
            { "Name_lang", 1, true }, { "ID" }, { "SecondaryID" }, { "Flags" }, { "ChrModelID" },
            { "OrderIndex" }, { "ChrCustomizationCategoryID" }, { "OptionType" },
            { "BarberShopCostModifier" }, { "ChrCustomizationID" }, { "Requirement" },
            { "SecondaryOrderIndex" }, { "AddedInPatch" },
        };
        constexpr Definition kChrCustomizationOption{
            "chrcustomizationoption", "DBFilesClient\\chrcustomizationoption.db2", 0xDCC2A86Eu,
            kChrCustomizationOptionFields,
        };

        constexpr Field kChrCustomizationChoiceFields[] = {
            { "Name_lang", 1, true }, { "ID" }, { "ChrCustomizationOptionID" }, { "ChrCustomizationReqID" },
            { "ChrCustomizationVisReqID" }, { "OrderIndex" }, { "UiOrderIndex" }, { "Flags" },
            { "AddedInPatch" }, { "SoundKitID" }, { "SwatchColor", 2 },
        };
        constexpr Definition kChrCustomizationChoice{
            "chrcustomizationchoice", "DBFilesClient\\chrcustomizationchoice.db2", 0x9559C358u,
            kChrCustomizationChoiceFields,
        };

        constexpr Field kChrCustomizationElementFields[] = {
            { "ChrCustomizationChoiceID" }, { "RelatedChrCustomizationChoiceID" },
            { "ChrCustomizationGeosetID" }, { "ChrCustomizationSkinnedModelID" },
            { "ChrCustomizationMaterialID" }, { "ChrCustomizationBoneSetID" },
            { "ChrCustomizationCondModelID" }, { "ChrCustomizationDisplayInfoID" },
            { "ChrCustItemGeoModifyID" }, { "ChrCustomizationVoiceID" }, { "AnimKitID" },
            { "ParticleColorID" }, { "ChrCustGeoComponentLinkID" },
        };
        constexpr Definition kChrCustomizationElement{
            "chrcustomizationelement", "DBFilesClient\\chrcustomizationelement.db2", 0x6483C37Eu,
            kChrCustomizationElementFields,
        };

        constexpr Field kChrCustomizationGeosetFields[] = {
            { "GeosetType" }, { "GeosetID" }, { "Modifier" },
        };
        constexpr Definition kChrCustomizationGeoset{
            "chrcustomizationgeoset", "DBFilesClient\\chrcustomizationgeoset.db2", 0xBF55F2FBu,
            kChrCustomizationGeosetFields,
        };

        constexpr Field kChrCustomizationSkinnedModelFields[] = {
            { "CollectionsFileDataID" }, { "GeosetType" }, { "GeosetID" }, { "Modifier" }, { "Flags" },
        };
        constexpr Definition kChrCustomizationSkinnedModel{
            "chrcustomizationskinnedmodel", "DBFilesClient\\chrcustomizationskinnedmodel.db2",
            0x4C32AA8Au, kChrCustomizationSkinnedModelFields,
        };

        constexpr Field kChrCustomizationMaterialFields[] = {
            { "ChrModelTextureTargetID" }, { "MaterialResourcesID" },
        };
        constexpr Definition kChrCustomizationMaterial{
            "chrcustomizationmaterial", "DBFilesClient\\chrcustomizationmaterial.db2", 0xBE9767E9u,
            kChrCustomizationMaterialFields,
        };

        constexpr Field kChrCustomizationCondModelFields[] = {
            { "CreatureModelDataID" }, { "Field_9_0_1_34081_001", 4 },
        };
        constexpr Definition kChrCustomizationCondModel{
            "chrcustomizationcondmodel", "DBFilesClient\\chrcustomizationcondmodel.db2",
            0x601A4522u, kChrCustomizationCondModelFields,
        };

        // Declared because kChrModelTextureLayer names it as a relation target, and a target that is
        // not declared fails validation for the WHOLE set: one missing table is the difference
        // between every appearance resolving and none of them resolving.
        constexpr Field kCharComponentTextureLayoutsFields[] = {
            { "Width" }, { "Height" },
        };
        constexpr Definition kCharComponentTextureLayouts{
            "charcomponenttexturelayouts", "DBFilesClient\\charcomponenttexturelayouts.db2",
            0x0B9AF134u, kCharComponentTextureLayoutsFields,
        };

        constexpr Field kChrModelTextureLayerFields[] = {
            { "TextureType" }, { "Layer" }, { "Flags" }, { "BlendMode" },
            { "TextureSectionTypeBitMask" }, { "TextureSectionTypeBitMask2" },
            { "Field_9_0_1_34365_006", 3 }, { "ChrModelTextureTargetID", 2 },
        };
        constexpr Relation kChrModelTextureLayerRelations[] = {
            { "CharComponentTextureLayoutsID", RelationSource::ParentId, {}, 0,
              "charcomponenttexturelayouts" },
        };
        constexpr Definition kChrModelTextureLayer{
            "chrmodeltexturelayer", "DBFilesClient\\chrmodeltexturelayer.db2", 0xD0583FB4u,
            kChrModelTextureLayerFields, kChrModelTextureLayerRelations,
        };

        constexpr Field kCreatureDisplayInfoFields[] = {
            { "ID" }, { "ModelID" }, { "SoundID" }, { "SizeClass" }, { "CreatureModelScale" },
            { "CreatureModelAlpha" }, { "BloodID" }, { "ExtendedDisplayInfoID" }, { "NPCSoundID" },
            { "ParticleColorID" }, { "PortraitCreatureDisplayInfoID" },
            { "PortraitTextureFileDataID" }, { "ObjectEffectPackageID" }, { "AnimReplacementSetID" },
            { "Flags" }, { "StateSpellVisualKitID" }, { "PlayerOverrideScale" },
            { "PetInstanceScale" }, { "UnarmedWeaponType" }, { "MountPoofSpellVisualKitID" },
            { "DissolveEffectID" }, { "Gender" }, { "DissolveOutEffectID" },
            { "CreatureModelMinLod" }, { "ConditionalCreatureModelID" }, { "MountMaxBankingAngle" },
            { "Field_11_0_0_54210_026" }, { "TextureVariationFileDataID", 4 },
        };
        constexpr Definition kCreatureDisplayInfo{
            "creaturedisplayinfo", "DBFilesClient\\creaturedisplayinfo.db2", 0x7275F5F6u,
            kCreatureDisplayInfoFields,
        };

        constexpr Field kCreatureModelDataFields[] = {
            { "GeoBox", 6 }, { "Flags" }, { "FileDataID" }, { "WalkSpeed" }, { "RunSpeed" },
            { "BloodID" }, { "FootprintTextureID" }, { "FootprintTextureLength" },
            { "FootprintTextureWidth" }, { "FootprintParticleScale" }, { "FoleyMaterialID" },
            { "FootstepCameraEffectID" }, { "DeathThudCameraEffectID" }, { "SoundID" },
            { "SizeClass" }, { "CollisionWidth" }, { "CollisionHeight" }, { "WorldEffectScale" },
            { "CreatureGeosetDataID" }, { "HoverHeight" }, { "AttachedEffectScale" },
            { "ModelScale" }, { "MissileCollisionRadius" }, { "MissileCollisionPush" },
            { "MissileCollisionRaise" }, { "MountHeight" }, { "OverrideLootEffectScale" },
            { "OverrideNameScale" }, { "OverrideSelectionRadius" }, { "TamedPetBaseScale" },
            { "MountScaleOtherIndex" }, { "MountScaleSelf" }, { "Field_11_0_0_54210_032" },
            { "MountScaleOther", 2 },
        };
        constexpr Definition kCreatureModelData{
            "creaturemodeldata", "DBFilesClient\\creaturemodeldata.db2", 0x0F5449F8u,
            kCreatureModelDataFields,
        };

        constexpr Field kCreatureDisplayInfoGeosetDataFields[] = {
            { "GeosetIndex" }, { "GeosetValue" },
        };
        constexpr Relation kCreatureDisplayInfoGeosetDataRelations[] = {
            { "CreatureDisplayInfoID", RelationSource::ParentId, {}, 0, "creaturedisplayinfo" },
        };
        constexpr Definition kCreatureDisplayInfoGeosetData{
            "creaturedisplayinfogeosetdata", "DBFilesClient\\creaturedisplayinfogeosetdata.db2",
            0x5E539080u, kCreatureDisplayInfoGeosetDataFields, kCreatureDisplayInfoGeosetDataRelations,
        };

        // Where each piece of a composited character sheet goes. The column order is not guessed: the
        // file's own values name them. Field 0 repeats the layout id the relationship column already
        // carries, field 1 runs 0..14 across every layout, and the four that follow tile each layout
        // exactly -- layout 1's rows cover 1024x1024 with no gap and no overlap, which is the size
        // CharComponentTextureLayouts states for it.
        constexpr Field kCharComponentTextureSectionsFields[] = {
            { "CharComponentTextureLayoutID" }, { "SectionType" },
            { "X" }, { "Y" }, { "Width" }, { "Height" }, { "OverlapSectionMask" },
        };
        constexpr Relation kCharComponentTextureSectionsRelations[] = {
            { "LayoutID", RelationSource::ParentId, {}, 0, "charcomponenttexturelayouts" },
        };
        constexpr Definition kCharComponentTextureSections{
            "charcomponenttexturesections", "DBFilesClient\\charcomponenttexturesections.db2",
            0x2173BA71u, kCharComponentTextureSectionsFields, kCharComponentTextureSectionsRelations,
        };

        constexpr Field kTextureFileDataFields[] = {
            { "ID" }, { "UsageType" }, { "MaterialResourcesID" },
        };
        constexpr Definition kTextureFileData{
            "texturefiledata", "DBFilesClient\\texturefiledata.db2", 0xBD7C74C2u, kTextureFileDataFields,
        };

        constexpr Definition kDefinitions[] = {
            kChrRaceXChrModel, kChrModel, kChrCustomizationOption, kChrCustomizationChoice,
            kChrCustomizationElement, kChrCustomizationGeoset, kChrCustomizationSkinnedModel,
            kChrCustomizationMaterial, kChrModelTextureLayer, kCreatureDisplayInfo,
            kCreatureModelData, kCreatureDisplayInfoGeosetData, kCharComponentTextureLayouts,
            kCharComponentTextureSections, kChrCustomizationCondModel,
        };

        // Loaded on first material lookup: 200k rows nobody needs until a texture is composed.
        Table g_textureFileData;
        std::unordered_map<uint32_t, std::vector<std::pair<uint32_t, uint32_t>>> g_materialFiles;
        std::once_flag g_textureFileOnce;

        void LoadTextureFileData()
        {
            std::string error;
            if (!g_textureFileData.Load(kTextureFileData, &error))
            {
                AppearanceLog("texturefiledata: %s", error.c_str());
                return;
            }
            for (const wdc5::Row& row : g_textureFileData.Rows())
                g_materialFiles[g_textureFileData.Value(row, "MaterialResourcesID")].push_back(
                    { g_textureFileData.Value(row, "UsageType"), row.id });
        }

        // --- resident state ----------------------------------------------------------------------
        StoreStatus g_status;
        std::once_flag g_loadOnce;

        Table g_raceModel, g_chrModel, g_option, g_choice, g_element, g_geoset, g_skinnedModel, g_condModel;
        Table g_material, g_textureLayer, g_displayInfo, g_modelData, g_creatureGeoset;
        Table g_textureLayout, g_textureSection;

        /// (race, sex) -> ChrModel id. Two integers with a natural join and no table of their own.
        std::unordered_map<uint64_t, uint32_t> g_modelByRaceSex;
        /// The rows each key fans out to. Built once; the tables never move afterwards.
        std::unordered_map<uint32_t, std::vector<const wdc5::Row*>> g_elementsByChoice;
        std::unordered_map<uint32_t, std::vector<const wdc5::Row*>> g_layersByLayout;
        std::unordered_map<uint32_t, std::vector<const wdc5::Row*>> g_geosetsByDisplay;
        std::unordered_map<uint32_t, std::vector<const wdc5::Row*>> g_sectionsByLayout;
        /// These two are enumerated by position, so they are sorted at build time: the caller's
        /// index has to mean the same thing every run, and row order in the file does not.
        std::unordered_map<uint32_t, std::vector<const wdc5::Row*>> g_optionsByModel;
        std::unordered_map<uint32_t, std::vector<const wdc5::Row*>> g_choicesByOption;

        constexpr uint64_t RaceSexKey(uint32_t race, uint32_t sex)
        {
            return (static_cast<uint64_t>(race) << 32) | sex;
        }

        /// A geoset group and its chosen value become the one number a submesh carries: the tables
        /// express the pair, the model only ever knows the product.
        ///
        /// Solid for a character, where the group is stated outright as a geoset TYPE. Applied to a
        /// creature on the strength of the analogy alone -- CreatureDisplayInfoGeosetData states an
        /// INDEX, and whether that index is the group itself or one less than it has not been checked
        /// against a model. If creature parts come out shifted by exactly one group, this is why.
        constexpr uint16_t GeosetId(uint32_t group, uint32_t value)
        {
            return static_cast<uint16_t>(group * 100u + value);
        }

        /// Puts each fan-out in the order the tables intend, so a caller's index is stable. Ties break
        /// on row id, because two rows sharing an OrderIndex must still not swap between runs.
        void SortByOrderIndex(std::unordered_map<uint32_t, std::vector<const wdc5::Row*>>& index,
                              const Table& table)
        {
            for (auto& [key, rows] : index)
            {
                std::sort(rows.begin(), rows.end(),
                          [&table](const wdc5::Row* a, const wdc5::Row* b)
                          {
                              const uint32_t oa = table.Value(*a, "OrderIndex");
                              const uint32_t ob = table.Value(*b, "OrderIndex");
                              return oa != ob ? oa < ob : a->id < b->id;
                          });
            }
        }

        void Load()
        {
            std::string error;
            if (!ValidateDefinitions(kDefinitions, &error))
            {
                g_status.failed = true;
                g_status.error = error;
                AppearanceLog("%s", error.c_str());
                return;
            }

            const struct { Table* table; const Definition* def; uint32_t* rows; } kLoads[] = {
                { &g_raceModel,      &kDefinitions[0],  &g_status.raceModels      },
                { &g_chrModel,       &kDefinitions[1],  &g_status.chrModels       },
                { &g_option,         &kDefinitions[2],  &g_status.options         },
                { &g_choice,         &kDefinitions[3],  &g_status.choices         },
                { &g_element,        &kDefinitions[4],  &g_status.elements        },
                { &g_geoset,         &kDefinitions[5],  &g_status.geosets         },
                { &g_skinnedModel,   &kDefinitions[6],  &g_status.skinnedModels   },
                { &g_material,       &kDefinitions[7],  &g_status.materials       },
                { &g_textureLayer,   &kDefinitions[8],  &g_status.textureLayers   },
                { &g_displayInfo,    &kDefinitions[9],  &g_status.displayInfos    },
                { &g_modelData,      &kDefinitions[10], &g_status.modelData       },
                { &g_creatureGeoset, &kDefinitions[11], &g_status.creatureGeosets },
                { &g_textureLayout,  &kDefinitions[12], &g_status.textureLayouts  },
                { &g_textureSection, &kDefinitions[13], &g_status.textureSections },
                { &g_condModel,      &kDefinitions[14], &g_status.conditionalModels },
            };

            bool allOk = true;
            for (const auto& load : kLoads)
            {
                std::string tableError;
                if (load.table->Load(*load.def, &tableError))
                {
                    *load.rows = static_cast<uint32_t>(load.table->Rows().size());
                    continue;
                }
                // Creature tables may be encrypted without the matching TACT key; characters
                // then resolve everything except their model file.
                const bool optional = load.table == &g_displayInfo || load.table == &g_creatureGeoset;
                if (!optional) allOk = false;
                if (!g_status.error.empty()) g_status.error += " | ";
                g_status.error += std::string(load.def->name) + ": " + tableError;
            }

            g_status.loaded = allOk;
            g_status.failed = !allOk;
            if (!allOk)
            {
                AppearanceLog("%s", g_status.error.c_str());
                return;
            }

            for (const wdc5::Row& row : g_raceModel.Rows())
            {
                const uint64_t key = RaceSexKey(g_raceModel.Value(row, "ChrRacesID"),
                                                g_raceModel.Value(row, "Sex"));
                g_modelByRaceSex[key] = g_raceModel.Value(row, "ChrModelID");
            }
            for (const wdc5::Row& row : g_element.Rows())
                g_elementsByChoice[g_element.Value(row, "ChrCustomizationChoiceID")].push_back(&row);
            for (const wdc5::Row& row : g_textureLayer.Rows())
                g_layersByLayout[row.parentId].push_back(&row);
            for (const wdc5::Row& row : g_creatureGeoset.Rows())
                g_geosetsByDisplay[row.parentId].push_back(&row);
            // Keyed on the stated layout rather than the relation's parent: the parent is what the
            // file's own foreign key resolves to, and a section that names its layout in a field
            // would otherwise be filed under nothing.
            for (const wdc5::Row& row : g_textureSection.Rows())
            {
                const uint32_t layout = g_textureSection.Value(row, "CharComponentTextureLayoutID");
                g_sectionsByLayout[layout ? layout : row.parentId].push_back(&row);
            }

            for (const wdc5::Row& row : g_option.Rows())
                g_optionsByModel[g_option.Value(row, "ChrModelID")].push_back(&row);
            for (const wdc5::Row& row : g_choice.Rows())
                g_choicesByOption[g_choice.Value(row, "ChrCustomizationOptionID")].push_back(&row);
            SortByOrderIndex(g_optionsByModel, g_option);
            SortByOrderIndex(g_choicesByOption, g_choice);

            AppearanceLog("%u chr model(s) over %u race/sex pair(s), %u option(s) and"
                      " %u choice(s) expanding to %u element(s), %u creature display(s) |"
                      " %u sheet layout(s) tiled by %u section(s)",
                      g_status.chrModels, static_cast<unsigned>(g_modelByRaceSex.size()),
                      g_status.options, g_status.choices, g_status.elements, g_status.displayInfos,
                      g_status.textureLayouts, g_status.textureSections);
        }

        void EnsureLoaded()
        {
            std::call_once(g_loadOnce, &Load);
        }

        uint32_t ModelFileFromDisplay(uint32_t displayId)
        {
            const wdc5::Row* display = g_displayInfo.Find(displayId);
            if (!display) return 0;
            const wdc5::Row* model = g_modelData.Find(g_displayInfo.Value(*display, "ModelID"));
            return model ? g_modelData.Value(*model, "FileDataID") : 0;
        }

        void AddMaterialLayers(uint32_t materialId, uint32_t layoutId, Recipe& out)
        {
            const wdc5::Row* material = g_material.Find(materialId);
            if (!material) return;

            const uint32_t target = g_material.Value(*material, "ChrModelTextureTargetID");
            const uint32_t resource = g_material.Value(*material, "MaterialResourcesID");

            const auto layers = g_layersByLayout.find(layoutId);
            if (layers == g_layersByLayout.end()) return;

            for (const wdc5::Row* layer : layers->second)
            {
                if (g_textureLayer.Value(*layer, "ChrModelTextureTargetID") != target) continue;
                out.layers.push_back({
                    g_textureLayer.Value(*layer, "TextureType"),
                    g_textureLayer.Value(*layer, "Layer"),
                    g_textureLayer.Value(*layer, "BlendMode"),
                    g_textureLayer.Value(*layer, "TextureSectionTypeBitMask"),
                    resource,
                });
            }
        }

        /**
         * @brief Everything one taken choice contributes, filtered by what else was taken.
         *
         * An element may name a SECOND choice it depends on, which is how two options cross: a hair
         * style lists one element per hair colour, each waiting for its colour to be chosen too.
         * Taking them all hands back every colour of that style at once, stacked -- nineteen of them
         * for one human hairstyle.
         */
        void AddChoice(uint32_t choiceId, uint32_t layoutId, const uint32_t* selected,
                       uint32_t selectedCount, Recipe& out, bool allowModelSwap)
        {
            const auto elements = g_elementsByChoice.find(choiceId);
            if (elements == g_elementsByChoice.end()) return;
            bool visibleCollection = false, blindfoldMaterial = false;

            for (const wdc5::Row* element : elements->second)
            {
                const uint32_t needs = g_element.Value(*element, "RelatedChrCustomizationChoiceID");
                if (needs)
                {
                    bool alsoTaken = false;
                    for (uint32_t i = 0; i < selectedCount && !alsoTaken; ++i)
                        alsoTaken = selected[i] == needs;
                    if (!alsoTaken) continue;
                }

                if (const uint32_t id = g_element.Value(*element, "ChrCustomizationGeosetID"))
                {
                    if (const wdc5::Row* geoset = g_geoset.Find(id))
                        out.geosets.push_back(GeosetId(g_geoset.Value(*geoset, "GeosetType"),
                                                       g_geoset.Value(*geoset, "GeosetID")));
                }
                if (const uint32_t id = g_element.Value(*element, "ChrCustomizationSkinnedModelID"))
                {
                    if (const wdc5::Row* skinned = g_skinnedModel.Find(id)) {
                        out.attached.push_back({
                            g_skinnedModel.Value(*skinned, "CollectionsFileDataID"),
                            GeosetId(g_skinnedModel.Value(*skinned, "GeosetType"),
                                     g_skinnedModel.Value(*skinned, "GeosetID")),
                        });
                        visibleCollection = visibleCollection || (out.attached.back().fileDataId && out.attached.back().geoset % 100);
                    }
                }
                if (const uint32_t id = g_element.Value(*element, "ChrCustomizationMaterialID")) {
                    AddMaterialLayers(id, layoutId, out);
                    const auto* material = g_material.Find(id);
                    blindfoldMaterial = blindfoldMaterial || (material && g_material.Value(*material, "ChrModelTextureTargetID") == 40);
                }
                if (const uint32_t id = allowModelSwap ? g_element.Value(*element, "ChrCustomizationCondModelID") : 0) {
                    if (const auto* conditional = g_condModel.Find(id)) {
                        bool unconditional = true;
                        for (uint32_t i = 0; i < 4; ++i)
                            unconditional = unconditional && !g_condModel.Value(*conditional, "Field_9_0_1_34081_001", i);
                        const auto* model = g_modelData.Find(g_condModel.Value(*conditional, "CreatureModelDataID"));
                        if (unconditional && model && g_modelData.Value(*model, "FileDataID"))
                            out.modelFileDataId = g_modelData.Value(*model, "FileDataID");
                    }
                }
            }
            // Shared Elf blindfolds are offered with ordinary eye colors, but the new palette
            // materials are gated to Demon Hunter eye choices on a different ChrModel. If none
            // matches, use the first authored blindfold palette without changing selected eyes.
            // Limit this to target 40 on a visible selected collection.
            if (visibleCollection && !blindfoldMaterial) {
                uint32_t fallback = 0, option = 0, order = UINT32_MAX;
                bool compatible = true;
                for (const auto* element : elements->second) {
                    const auto* related = g_choice.Find(g_element.Value(*element, "RelatedChrCustomizationChoiceID"));
                    const uint32_t id = g_element.Value(*element, "ChrCustomizationMaterialID");
                    const auto* material = g_material.Find(id);
                    if (!related || !material || g_material.Value(*material, "ChrModelTextureTargetID") != 40) continue;
                    const uint32_t current = g_choice.Value(*related, "ChrCustomizationOptionID");
                    if (option && option != current) compatible = false;
                    option = current;
                    const uint32_t rank = g_choice.Value(*related, "OrderIndex");
                    if (rank < order) { order = rank; fallback = id; }
                }
                if (compatible && fallback) AddMaterialLayers(fallback, layoutId, out);
            }
        }
    }

    const StoreStatus& Status()
    {
        EnsureLoaded();
        return g_status;
    }

    bool BuildForCharacter(uint32_t chrRaceId, uint32_t sex, const uint32_t* choiceIds,
                           uint32_t choiceCount, Recipe& out)
    {
        out.Clear();
        EnsureLoaded();
        if (!g_status.loaded) return false;

        const auto pair = g_modelByRaceSex.find(RaceSexKey(chrRaceId, sex));
        if (pair == g_modelByRaceSex.end()) return false;

        const wdc5::Row* model = g_chrModel.Find(pair->second);
        if (!model) return false;

        out.skeletonFileDataId = g_chrModel.Value(*model, "SkeletonFileDataID");
        out.textureLayoutId    = g_chrModel.Value(*model, "CharComponentTextureLayoutID");
        out.modelFileDataId    = ModelFileFromDisplay(g_chrModel.Value(*model, "DisplayID"));

        for (uint32_t i = 0; i < choiceCount; ++i)
            AddChoice(choiceIds[i], out.textureLayoutId, choiceIds, choiceCount, out,
                sex == 0 && (chrRaceId == 2 || chrRaceId == 36)); // Orc posture; other conditional forms need separate integration.

        std::stable_sort(out.layers.begin(), out.layers.end(),
                         [](const TextureLayer& a, const TextureLayer& b) {
                             return a.layer < b.layer;
                         });

        return out.modelFileDataId != 0;
    }

    bool BuildForCreature(uint32_t creatureDisplayInfoId, Recipe& out)
    {
        out.Clear();
        EnsureLoaded();
        if (!g_status.loaded) return false;

        out.modelFileDataId = ModelFileFromDisplay(creatureDisplayInfoId);
        if (!out.modelFileDataId) return false;

        const auto geosets = g_geosetsByDisplay.find(creatureDisplayInfoId);
        if (geosets != g_geosetsByDisplay.end())
            for (const wdc5::Row* row : geosets->second)
                out.geosets.push_back(GeosetId(g_creatureGeoset.Value(*row, "GeosetIndex"),
                                               g_creatureGeoset.Value(*row, "GeosetValue")));

        return true;
    }

    uint32_t ChrModelForRace(uint32_t chrRaceId, uint32_t sex)
    {
        EnsureLoaded();
        if (!g_status.loaded) return 0;
        const auto pair = g_modelByRaceSex.find(RaceSexKey(chrRaceId, sex));
        return pair != g_modelByRaceSex.end() ? pair->second : 0;
    }

    uint32_t OptionCount(uint32_t chrModelId)
    {
        EnsureLoaded();
        if (!g_status.loaded) return 0;
        const auto options = g_optionsByModel.find(chrModelId);
        return options != g_optionsByModel.end() ? static_cast<uint32_t>(options->second.size()) : 0;
    }

    bool OptionAt(uint32_t chrModelId, uint32_t index, ChrOption& out)
    {
        EnsureLoaded();
        if (!g_status.loaded) return false;
        const auto options = g_optionsByModel.find(chrModelId);
        if (options == g_optionsByModel.end() || index >= options->second.size()) return false;

        const wdc5::Row& row = *options->second[index];
        out.id         = row.id;
        out.optionType = g_option.Value(row, "OptionType");
        out.orderIndex = g_option.Value(row, "OrderIndex");
        out.flags      = g_option.Value(row, "Flags");
        return true;
    }

    uint32_t ChoiceCount(uint32_t chrCustomizationOptionId)
    {
        EnsureLoaded();
        if (!g_status.loaded) return 0;
        const auto choices = g_choicesByOption.find(chrCustomizationOptionId);
        return choices != g_choicesByOption.end() ? static_cast<uint32_t>(choices->second.size()) : 0;
    }

    bool ChoiceAt(uint32_t chrCustomizationOptionId, uint32_t index, ChrChoice& out)
    {
        EnsureLoaded();
        if (!g_status.loaded) return false;
        const auto choices = g_choicesByOption.find(chrCustomizationOptionId);
        if (choices == g_choicesByOption.end() || index >= choices->second.size()) return false;

        const wdc5::Row& row = *choices->second[index];
        out.id           = row.id;
        out.orderIndex   = g_choice.Value(row, "OrderIndex");
        out.uiOrderIndex = g_choice.Value(row, "UiOrderIndex");
        out.flags        = g_choice.Value(row, "Flags");
        out.swatchColor  = g_choice.Value(row, "SwatchColor");
        return true;
    }

    uint32_t LayoutForModel(uint32_t chrModelId)
    {
        EnsureLoaded();
        if (!g_status.loaded) return 0;
        const wdc5::Row* model = g_chrModel.Find(chrModelId);
        return model ? g_chrModel.Value(*model, "CharComponentTextureLayoutID") : 0;
    }

    bool LayoutSize(uint32_t layoutId, uint32_t& outWidth, uint32_t& outHeight)
    {
        EnsureLoaded();
        if (!g_status.loaded) return false;

        for (const wdc5::Row& row : g_textureLayout.Rows())
        {
            if (row.id != layoutId) continue;
            outWidth  = g_textureLayout.Value(row, "Width");
            outHeight = g_textureLayout.Value(row, "Height");
            return outWidth != 0 && outHeight != 0;
        }
        return false;
    }

    uint32_t SectionCount(uint32_t layoutId)
    {
        EnsureLoaded();
        if (!g_status.loaded) return 0;
        const auto sections = g_sectionsByLayout.find(layoutId);
        return sections != g_sectionsByLayout.end()
             ? static_cast<uint32_t>(sections->second.size()) : 0;
    }

    bool SectionAt(uint32_t layoutId, uint32_t index, TextureSection& out)
    {
        EnsureLoaded();
        if (!g_status.loaded) return false;
        const auto sections = g_sectionsByLayout.find(layoutId);
        if (sections == g_sectionsByLayout.end() || index >= sections->second.size()) return false;

        const wdc5::Row& row = *sections->second[index];
        out.sectionType = g_textureSection.Value(row, "SectionType");
        out.x           = g_textureSection.Value(row, "X");
        out.y           = g_textureSection.Value(row, "Y");
        out.width       = g_textureSection.Value(row, "Width");
        out.height      = g_textureSection.Value(row, "Height");
        out.overlapMask = g_textureSection.Value(row, "OverlapSectionMask");
        return true;
    }

    uint32_t MaterialFileDataId(uint32_t materialResourcesId, uint32_t usage)
    {
        std::call_once(g_textureFileOnce, &LoadTextureFileData);
        const auto it = g_materialFiles.find(materialResourcesId);
        if (it == g_materialFiles.end()) return 0;
        for (const uint32_t wanted : { usage, 2u })
            for (const auto& candidate : it->second)
                if (candidate.first == wanted) return candidate.second;
        return it->second.front().second;
    }

    std::string OptionName(uint32_t optionId)
    {
        EnsureLoaded();
        if (!g_status.loaded) return {};
        const wdc5::Row* row = g_option.Find(optionId);
        return row ? g_option.Text(*row, "Name_lang") : std::string();
    }

    std::string ChoiceName(uint32_t choiceId)
    {
        EnsureLoaded();
        if (!g_status.loaded) return {};
        const wdc5::Row* row = g_choice.Find(choiceId);
        return row ? g_choice.Text(*row, "Name_lang") : std::string();
    }

    const Table* Get(std::string_view table)
    {
        EnsureLoaded();
        if (!g_status.loaded) return nullptr;

        const struct { std::string_view name; const Table* table; } kTables[] = {
            { "chrracexchrmodel",              &g_raceModel      },
            { "texturefiledata",               &g_textureFileData },
            { "chrmodel",                      &g_chrModel       },
            { "chrcustomizationoption",        &g_option         },
            { "chrcustomizationchoice",        &g_choice         },
            { "chrcustomizationelement",       &g_element        },
            { "chrcustomizationgeoset",        &g_geoset         },
            { "chrcustomizationskinnedmodel",  &g_skinnedModel   },
            { "chrcustomizationmaterial",      &g_material       },
            { "chrcustomizationcondmodel",     &g_condModel      },
            { "chrmodeltexturelayer",          &g_textureLayer   },
            { "creaturedisplayinfo",           &g_displayInfo    },
            { "creaturemodeldata",             &g_modelData      },
            { "creaturedisplayinfogeosetdata", &g_creatureGeoset },
            { "charcomponenttexturelayouts",   &g_textureLayout  },
            { "charcomponenttexturesections",  &g_textureSection },
        };
        for (const auto& entry : kTables)
            if (entry.name == table) return entry.table;
        return nullptr;
    }
}
