#ifndef ENGINE_SOURCE_LUMENVEIL_RENDERER_H
#define ENGINE_SOURCE_LUMENVEIL_RENDERER_H

#include "Engine/Source/Lumenveil/Core/belt_lantern.h"
#include "Engine/Source/Lumenveil/Core/config.h"
#include "Engine/Source/Lumenveil/Core/corpse_lights.h"
#include "Engine/Source/Lumenveil/Core/engine.h"
#include "Engine/Source/Lumenveil/Core/fog_model.h"
#include "Engine/Source/Lumenveil/Core/lamp_selection.h"
#include "Engine/Source/Lumenveil/Core/lamp_tiles.h"
#include "Engine/Source/Lumenveil/Core/shaft_memory.h"
#include "Engine/Source/Lumenveil/Core/terrain_shadow.h"
#include "Engine/Source/Lumenveil/Core/wet_surfaces.h"
#include "Engine/Source/Lumenveil/Core/water_lamps.h"
#include "Engine/Source/Lumenveil/Core/water_rain.h"
#include "Engine/Source/Lumenveil/Core/water_ripples.h"
#include "Engine/Source/Lumenveil/Core/water_waves.h"

#include <cstdint>
#include <vector>

#include "Engine/Source/Gx/GxEffects.h"

// Not in the original 3.3.5a binary. Lumenveil's fog over GxEffects: the fog and sun and moon shaft parts of the mod's
// Renderer (engine/src/renderer.cpp, RenderPasses), with its passes, constants and texture stages as the mod set them.
// The HLSL is the mod's own, compiled by Shaders/Compile.py.
//
// What this port draws: the depth copy, the fog march (layers, drift, banks, clouds, terrain shadows, the shaft
// memory seen from the light or, without it, the edge occluders), the temporal smoothing, the screen-space sun
// shafts, the light glow, the sky reference and the composite (fog over a scene copy, the colour grade and the night
// darkening).
// The night lights are here too: the lamps, windows, belt lantern and corpse lights (lampdata.bin), as halos and as
// light on the surfaces.
// Not yet: the spell lights, the wet surfaces, the client's own shadow maps and the late-draw and sun-and-moon
// captures, which need the draw log; the composite gets them as empty stages, as the mod does when they are off.
class LumenveilRenderer {
    public:
    // Draws the frame's fog onto the world's current target. False with a reason when it did not. occluderDepth is
    // the depth as it stood after the liquids (what casts shafts: no names, no spell effects), or null to use `depth`.
    bool Render(GxEffects::Device* device, GxEffects::Texture* depth, GxEffects::Texture* occluderDepth, uint32_t targetW,
                uint32_t targetH, const FrameInputs& in, const Config& cfg, const char** skip);
    // Everything goes: a new device, or the effects switched off.
    void Release();
    // The scene as it stands after the liquids, before the client's glowing effects draw: what the spell lights
    // compare the finished scene with. Call it when the world's target holds that scene.
    // Debug: false makes every pixel test every lamp.
    void SetLampTiles(bool on) { m_lampTilesWanted = on; }
    // The client's own shadows already shade the ground from the sun or moon with buildings and characters: the screen-space
    // shadow of objects stands down so the two do not double up.
    void SetStockObjectShadows(bool on) { m_stockObjectShadows = on; }
    // The water (Phase 4, so far its colour: depth absorption, clarity). The client draws liquids without writing
    // depth; Lumenveil::LiquidBegin forces the writes, so the depth after the liquids has the surface and the depth
    // before has what lies under it. CaptureWaterBefore at the start of the liquid pass, DrawWater at its end.
    bool CaptureWaterBefore(GxEffects::Device* device, const D3DVIEWPORT9& vp);
    bool DrawWater(GxEffects::Device* device, GxEffects::Texture* depthBefore, GxEffects::Texture* depthAfter, const FrameInputs& in, const Config& cfg);
    bool EnsureWaterRainTable();
    bool FillWaterRippleTable(const WaterRippleConstants& ripples);
    bool EnsureWaterMemory();
    bool UpdateWaterMemory(const FrameInputs& in, const Config& cfg);
    bool WaterSkip(const char* reason) {
        ++m_waterSkips;
        m_waterSkipReason = reason;
        return false;
    }
    void LogWater(const FrameInputs& in, const Config& cfg);
    bool UpdateFineShadowMemory(const FrameInputs& in, const struct CarrierBody& body);
    bool EnsureWetTarget(uint32_t w, uint32_t h);
    bool RenderWetSurfaces(GxEffects::Texture* fogTexture, const FrameInputs& in, const Config& cfg, const FogParams& fog, const TerrainShadowConstants& terrain,
                           const float* cloudMap, const float* cloudShape, float exposure, bool sceneBlend);
    bool EnsureWaterWaves();
    bool DrawWaterWaves(const WaterWaveField& field);
    bool EnsureWaterSlopeTarget(uint32_t w, uint32_t h);
    void CaptureEffectBaseline(GxEffects::Device* device, const D3DVIEWPORT9& vp);

    private:
    // D3D9's pipeline state as the mod's renderer sets it, carried from one draw to the next.
    struct State {
        float constants[GxEffects::kMaxConstantRows][4];
        GxEffects::Binding stages[GxEffects::kMaxTextures];
        GxEffects::Texture* target;
        int32_t x, y;
        uint32_t width, height;
        const GxEffects::Program* program;
        const GxEffects::Program* vertexProgram;   // with points: a point-list pass
        uint32_t points;
        bool clear;   // clear the target to zero first (D3D9's Clear before the draw)
        GxEffects::Blend blend;
        uint32_t writeMask;
    };

    void SetConstants(uint32_t reg, const float* values, uint32_t rows);
    void Bind(uint32_t stage, GxEffects::Texture* texture, bool linear, bool wrap = false, bool mips = false);
    void SetTarget(GxEffects::Texture* texture);
    void SetWorldTarget(const D3DVIEWPORT9& vp);
    bool DrawFullscreen();
    // D3D9's DrawPrimitive(D3DPT_POINTLIST) over a grid of gridW points a row, from a vertex program.
    bool DrawPoints(const GxEffects::Program* vertexProgram, uint32_t gridW, uint32_t points);

    bool Skip(const char* reason, const char** skip);
    bool EnsurePrograms();
    bool EnsureTargets(uint32_t lowW, uint32_t lowH, uint32_t rayW, uint32_t rayH);
    bool CopyWorldViewport(const D3DVIEWPORT9& vp);
    bool EnsureDriftNoise();
    bool EnsureCloudNoise();
    bool EnsureTerrainTexture();
    bool UploadTerrainShadows();
    TerrainShadowConstants UpdateTerrainShadows(const FrameInputs& in, const Config& cfg);
    FogCeilingConstants UpdateFogCeiling(const FrameInputs& in, const Config& cfg, bool floorsKnown);
    bool UploadBankFloors();
    void AdvanceDrift(const FrameInputs& in, const FogParams& fog, const Config& cfg);
    void AdvanceClouds(const FrameInputs& in, const FogParams& fog);
    bool RenderLightGlow(const D3DVIEWPORT9& vp, const Config& cfg);
    void RenderEdgeOccluders(const D3DVIEWPORT9& vp, const struct CarrierBody& body);
    bool EnsureShaftMemory(const D3DVIEWPORT9& vp);
    bool UpdateShaftMemory(const FrameInputs& in, const struct CarrierBody& body);
    // The night lights (renderer.cpp's RenderLamps and its helpers in the mod), drawn after the temporal pass.
    void RenderLamps(GxEffects::Texture* fogTexture, const FrameInputs& in, const Config& cfg, const FogParams& fog);
    bool EnsureLampTargets(uint32_t lowW, uint32_t lowH);
    bool EnsureLampSurfaceTarget(uint32_t w, uint32_t h);
    bool UploadLampTiles(const LampTable& table, const FrameInputs& in);
    void BlurLampLight(GxEffects::Texture* source, GxEffects::Texture* destination, float stepX, float stepY);
    // The spell lights: what the client's glowing effects added to the scene is summed on the GPU, read back a
    // frame or two later and turned into lights by the tracker (Core/effect_lights.cpp).
    bool EnsureEffectTargets(uint32_t w, uint32_t h);
    bool TakeEffectMeasurement();
    void GatherEffects(const FrameInputs& in);
    const std::vector<EffectLamp>* UpdateEffects(const FrameInputs& in, const FogParams& fog, bool gather);
    // The sun or moon's shadow of what is on screen, marched through the depth (not in the mod): true when drawn.
    bool RenderLightShadow(const FrameInputs& in, const float* toLightInView, bool memory);
    bool EnsureSkyReference();
    bool SkyReferenceFresh(const FrameInputs& in) const;
    void MeasureSky(const FrameInputs& in, const Config& cfg, const FogParams& fog);
    void LogSummary(const FrameInputs& in, const FogParams& fog, const float* viewToWorld, const float* toLightInView,
                    const float* sunPx, float rayStrength);

    GxEffects::Device* m_device = nullptr;
    State m_state = {};
    GxEffects::Texture* m_depth = nullptr;
    uint32_t m_targetW = 0;
    uint32_t m_targetH = 0;
    bool m_failed = false;

    GxEffects::Program* m_march[3] = {};
    GxEffects::Program* m_temporal = nullptr;
    GxEffects::Program* m_composite = nullptr;
    GxEffects::Program* m_rayMask = nullptr;
    GxEffects::Program* m_rayBlur = nullptr;
    GxEffects::Program* m_glowMask = nullptr;
    GxEffects::Program* m_glowBlur = nullptr;
    GxEffects::Program* m_edgeOccluder = nullptr;
    GxEffects::Program* m_skyGather = nullptr;
    GxEffects::Program* m_skyColumn = nullptr;
    GxEffects::Program* m_skyReferenceProgram = nullptr;
    GxEffects::Program* m_marchMemory[3] = {};
    GxEffects::Program* m_waterDepth = nullptr;
    GxEffects::Program* m_memoryScatterVs = nullptr;
    GxEffects::Program* m_memoryScatterPs = nullptr;
    GxEffects::Program* m_memoryRealignVs = nullptr;
    GxEffects::Program* m_memoryKeep = nullptr;
    GxEffects::Program* m_memoryFill = nullptr;

    GxEffects::Texture* m_marchTarget = nullptr;
    GxEffects::Texture* m_history[2] = {};
    GxEffects::Texture* m_rays[2] = {};
    uint32_t m_lowW = 0;
    uint32_t m_lowH = 0;
    uint32_t m_rayW = 0;
    uint32_t m_rayH = 0;
    GxEffects::Texture* m_sceneCopy = nullptr;
    uint32_t m_sceneCopyW = 0;
    uint32_t m_sceneCopyH = 0;
    bool m_sceneCopyFailed = false;

    GxEffects::Texture* m_driftNoise = nullptr;
    bool m_driftNoiseFailed = false;
    GxEffects::Texture* m_cloudNoise = nullptr;
    bool m_cloudNoiseFailed = false;
    TerrainShadowMap m_terrain;
    GxEffects::Texture* m_terrainTexture = nullptr;
    bool m_terrainFallback = false;
    unsigned m_terrainUploaded = 0;
    GxEffects::Texture* m_bankFloorTexture = nullptr;
    bool m_bankFloorFailed = false;
    bool m_bankFloorLogged = false;
    unsigned m_bankFloorUploaded = 0;
    float m_ceilingGate = 0.0f;
    double m_ceilingSeconds = -1.0;

    GxEffects::Texture* m_glowSource = nullptr;
    GxEffects::Texture* m_glow[2] = {};
    uint32_t m_glowSourceW = 0;
    uint32_t m_glowSourceH = 0;
    uint32_t m_glowW = 0;
    uint32_t m_glowH = 0;
    bool m_glowFailed = false;

    GxEffects::Texture* m_edgeOccluders = nullptr;
    uint32_t m_edgeW = 0;
    uint32_t m_edgeH = 0;
    bool m_edgeOccludersFailed = false;

    GxEffects::Texture* m_occluderSource = nullptr;
    GxEffects::Texture* m_occluderDepth = nullptr;
    uint32_t m_occluderDepthW = 0;
    uint32_t m_occluderDepthH = 0;
    bool m_occluderDepthFailed = false;

    GxEffects::Texture* m_memory[2] = {};
    GxEffects::Texture* m_memoryBefore = nullptr;
    uint32_t m_memoryGridW = 0;
    uint32_t m_memoryGridH = 0;
    int m_memoryIndex = 0;
    long long m_memoryCell[2] = {};
    ShaftMemoryFrame m_memoryFrame = { { 0.0, 1.0, 0.0 }, {}, { 0.0, 0.0, 1.0 }, {} };
    float m_memoryLookup[4][4] = {};
    ShaftMemoryFrame m_memoryBeforeFrame = { { 0.0, 1.0, 0.0 }, {}, { 0.0, 0.0, 1.0 }, {} };
    long long m_memoryBeforeCell[2] = {};
    float m_memoryBeforeLookup[4][4] = {};
    double m_memoryFadeStart = 0.0;
    double m_memoryFadeSeconds = 0.0;
    double m_memoryLastRealign = 0.0;
    bool m_memoryFading = false;
    bool m_memoryRealignedBefore = false;
    int m_memoryMapId = 0;
    bool m_memoryValid = false;
    bool m_memoryFailed = false;
    bool m_memoryChecked = false;
    bool m_memoryUnsupported = false;
    unsigned m_memoryRealignmentsTotal = 0;

    GxEffects::Texture* m_skyGatherTarget = nullptr;
    GxEffects::Texture* m_skyColumns = nullptr;
    GxEffects::Texture* m_skyReference[2] = {};
    int m_skyReferenceIndex = 0;
    bool m_skyReferenceValid = false;
    bool m_skyReferenceFailed = false;
    double m_skyReferenceSeconds = 0.0;

    GxEffects::Program* m_lampVisibilityProgram = nullptr;
    GxEffects::Program* m_lampProgram = nullptr;
    GxEffects::Program* m_lampSurfaceProgram = nullptr;
    GxEffects::Program* m_lampBlurProgram = nullptr;
    // The fine shadow memory (see UpdateFineShadowMemory): the shaft memory's state again, for a map of finer texels.
    struct FineShadowMap {
        GxEffects::Texture* tex[2] = {};
        GxEffects::Texture* before = nullptr;
        int index = 0;
        long long cell[2] = {};
        ShaftMemoryFrame frame = { { 0.0, 1.0, 0.0 }, {}, { 0.0, 0.0, 1.0 }, {} };
        ShaftMemoryFrame beforeFrame = { { 0.0, 1.0, 0.0 }, {}, { 0.0, 0.0, 1.0 }, {} };
        long long beforeCell[2] = {};
        float lookup[4][4] = {};
        float beforeLookup[4][4] = {};
        double fadeStart = 0.0;
        double fadeSeconds = 0.0;
        double lastRealign = 0.0;
        bool fading = false;
        bool realignedBefore = false;
        bool valid = false;
        bool failed = false;
        int mapId = 0;
        unsigned realignments = 0;
        uint32_t gridW = 0;
        uint32_t gridH = 0;
    };
    FineShadowMap m_fine;
    bool m_fineActive = false;
    GxEffects::Program* m_lightShadowProgram = nullptr;
    GxEffects::Program* m_waterProgram = nullptr;
    GxEffects::Program* m_waterReflectProgram = nullptr;
    GxEffects::Program* m_waterWaveProgram[kWaterWaveLayers] = {};
    GxEffects::Program* m_waterSlopeProgram = nullptr;
    GxEffects::Program* m_waterMemoryProgram = nullptr;
    GxEffects::Program* m_wetProgram = nullptr;
    GxEffects::Texture* m_wetTarget = nullptr;    // viewport-sized RGBA16F: wet sheen, puddles and reflections the composite adds
    uint32_t m_wetW = 0;
    uint32_t m_wetH = 0;
    bool m_wetFailed = false;
    bool m_wetDrawn = false;
    SurfaceWetness m_wetness;
    WetSurfaceParams m_wetParams = {};
    unsigned m_wetFrames = 0;
    float m_wetLampSlots = 0.0f;
    float m_wetLampTiles[4] = {};
    bool m_waterKnown = false;   // the water was drawn this frame: m_waterDepthBefore tells where
    GxEffects::Texture* m_waterDepthBefore = nullptr;
    GxEffects::Program* m_waterLampProgram = nullptr;
    GxEffects::Texture* m_waterLampTable = nullptr;   // 64 x 3 RGBA32F: the last frame's lamps and lit windows, as the water sees them
    WaterLampRecords m_waterLampRecords = {};
    bool m_waterLampFailed = false;
    bool m_waterLampsLogged = false;
    GxEffects::Texture* m_waterRainTable = nullptr;     // 256 x 256 BGRA8 drop table
    GxEffects::Texture* m_waterRippleTable = nullptr;   // 27 x 2 RGBA32F, filled each frame the player makes ripples
    bool m_waterRainFailed = false;
    bool m_waterRippleFailed = false;
    WaterRipples m_waterRipples;
    GxEffects::Texture* m_waterMemory[2] = {};   // octahedral maps of the point and colour the camera saw in each direction
    int m_waterMemoryIndex = 0;
    bool m_waterMemoryValid = false;
    bool m_waterMemoryOn = false;
    bool m_waterMemoryFailed = false;
    float m_waterMemoryCamPos[3] = {};
    double m_waterMemorySeconds = 0.0;
    int m_waterMemoryMapId = -1;
    unsigned m_waterMemoryForgets = 0;
    GxEffects::Texture* m_waterWaveTable = nullptr;
    GxEffects::Texture* m_waterWaves[kWaterWaveLayers] = {};   // each layer's levels stacked in one atlas, 256 x 511
    GxEffects::Texture* m_waterSlopeTarget = nullptr;
    uint32_t m_waterSlopeW = 0;
    uint32_t m_waterSlopeH = 0;
    bool m_waterWavesFailed = false;
    GxEffects::Texture* m_waterSlopes = nullptr;   // 1x1: no slope, the roughness of the unresolved waves (no wave textures yet)
    float m_waterSlopesRoughness = -1.0f;
    GxEffects::Texture* m_waterScene[2] = {};
    uint32_t m_waterSceneW = 0;
    uint32_t m_waterSceneH = 0;
    bool m_waterBeforeFresh = false;
    bool m_waterFailed = false;
    unsigned m_waterDrawn = 0;
    unsigned m_waterSkips = 0;
    const char* m_waterSkipReason = "";
    unsigned m_waterWaveFrames = 0;
    unsigned m_waterMemoryFrames = 0;
    unsigned m_waterLampFrames = 0;
    unsigned m_waterRainFrames = 0;
    unsigned m_waterRippleStarts = 0;
    GxEffects::Texture* m_lightShadow[2] = {};
    uint32_t m_lightShadowW = 0;
    uint32_t m_lightShadowH = 0;
    bool m_lightShadowFailed = false;
    LampSelection m_lamps;
    LampTiles m_lampTiles;
    BeltLantern m_lantern;
    CorpseLights m_corpses;
    GxEffects::Texture* m_lampTable = nullptr;
    GxEffects::Texture* m_lampVisibility[2] = {};
    GxEffects::Texture* m_lampHalos = nullptr;
    GxEffects::Texture* m_lampSurfaces = nullptr;
    GxEffects::Texture* m_lampSurfacesBlur = nullptr;
    GxEffects::Texture* m_lampTileTexture = nullptr;
    std::vector<uint8_t> m_lampTileBytes;
    uint32_t m_lampW = 0;
    uint32_t m_lampH = 0;
    uint32_t m_lampSurfaceW = 0;
    uint32_t m_lampSurfaceH = 0;
    uint32_t m_lampTileW = 0;
    uint32_t m_lampTileH = 0;
    int m_lampVisibilityIndex = 0;
    bool m_lampsDrawn = false;
    bool m_lampSurfacesDrawn = false;
    bool m_lampsFailed = false;
    bool m_lampSurfacesFailed = false;
    bool m_lampTilesFailed = false;
    bool m_lampTilesWanted = true;
    bool m_stockObjectShadows = false;

    struct EffectReadback {
        GxEffects::Texture* target = nullptr;
        GxEffects::Readback* read = nullptr;
        unsigned issuedFrame = 0;
        unsigned order = 0;
        EffectFrame frame = {};
    };
    GxEffects::Program* m_effectGatherLight = nullptr;
    GxEffects::Program* m_effectGatherPlace = nullptr;
    GxEffects::Program* m_effectReduce = nullptr;
    GxEffects::Texture* m_effectBaseline = nullptr;
    GxEffects::Texture* m_effectGather[2] = {};
    EffectReadback m_effectRing[kEffectReadbackRing];
    EffectTracker m_effectTracker;
    EffectMeasurement m_effectMeasurement = {};
    std::vector<EffectLamp> m_carried;
    uint32_t m_effectBaselineW = 0;
    uint32_t m_effectBaselineH = 0;
    uint32_t m_effectW = 0;
    uint32_t m_effectH = 0;
    uint32_t m_effectGatherW = 0;
    uint32_t m_effectGatherH = 0;
    uint32_t m_effectColumns = 0;
    uint32_t m_effectRows = 0;
    unsigned m_effectOrder = 0;
    bool m_effectBaselineFresh = false;
    bool m_effectsFailed = false;
    unsigned m_effectGathers = 0;
    unsigned m_effectMeasurements = 0;
    unsigned m_effectRingFull = 0;

    double m_driftSeconds = 0.0;
    bool m_driftClockValid = false;
    double m_coarseDrift[3] = {};
    double m_fineDrift[3] = {};
    double m_cloudSeconds = 0.0;
    bool m_cloudClockValid = false;
    double m_cloudOffset[2] = {};

    int m_historyIndex = 0;
    bool m_historyValid = false;
    float m_prevWorldToView[16] = {};
    float m_prevProj[16] = {};
    float m_prevCam[3] = {};
    D3DVIEWPORT9 m_prevViewport = {};
    uint32_t m_prevScale = 0;
    double m_prevSeconds = 0.0;
    unsigned m_frame = 0;
    double m_summarySeconds = -1.0e9;
    int m_summaryLight = -1;   // the light the last summary was for: 0 sun, 1 moon
    float m_loggedBlendMode = -1.0f;
};

#endif
