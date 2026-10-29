#pragma once

// Replaces Lumenveil's engine.h (see README.md). The stock client's addresses and the D3D9 device are gone: the
// frame inputs are read from the client's own source (WoW/Source/Ui/LumenveilInputs.cpp), and what the vendored
// files use from this header is kept with the same names.

#include <windows.h>
#include <d3d9types.h>

#include "fog_data.h"

#include <cstdint>

enum class WeatherKind
{
    Clear,
    Rain,
    Snow,
    Sand,
};

constexpr int kMaxCorpseInputs = 16;
constexpr int kMaxUnitInputs = 32;

struct UnitInput
{
    float feet[3];
    float boundingRadius;
    int target;
    uint64_t guid;
};

struct FrameInputs
{
    float cameraRelativeView[16];
    float glProjection[16];
    float camPos[3];
    float camTarget[3];
    D3DVIEWPORT9 viewport;
    double seconds;
    float dayFraction;
    float toLight[3];
    bool lightIsMoon;
    float sunElevation;
    float moonElevation;
    WeatherKind weather;
    float weatherIntensity;
    uint32_t fogColor;
    uint32_t sunColor;
    uint32_t directColor;
    uint32_t ambientColor;
    float fogStart;
    float fogEnd;
    float zoneFogDistance;
    float farClip;
    float clientFarClip;
    float clientGlowAmount;
    bool inLiquid;
    int mapId;
    int zoneId;
    uint32_t skyHorizonColor;
    uint32_t skyAboveHorizonColor;
    float dryLand;
    float greenLand;
    LightParamsSelection lightParams;
    float indoors;
    bool playerKnown;
    float playerPos[3];
    float playerFacing;
    uint32_t playerMoveFlags;
    uint32_t playerMountDisplay;
    uint32_t playerClass;
    bool targetKnown;
    float targetPos[3];
    float targetBoundingRadius;
    int corpseCount;
    float corpsePos[kMaxCorpseInputs][3];
    uint32_t corpseIds[kMaxCorpseInputs];
    int unitCount;
    UnitInput units[kMaxUnitInputs];
};

namespace engine
{
using FarClipSetFn = void(__cdecl*)(float farClipSetting);

constexpr uint32_t kMovementSwimming = 0x00200000;
constexpr double kIndoorFadeSeconds = 0.5;
constexpr float kUnitScanYards = 80.0f;
constexpr float kCorpseScanYards = 60.0f;
constexpr double kCorpseScanSeconds = 0.1;

constexpr int kDayNightFogGroupCount = 2;
constexpr int kFrameInputsFogGroup = 1;

struct StockFog
{
    float start[kDayNightFogGroupCount];
    float end[kDayNightFogGroupCount];
};
StockFog ReadStockFog();
void WriteStockFog(const StockFog& fog);

bool CameraInLiquid();
float IndoorWeight(float current, bool indoors, double stepSeconds);
bool ProbeGroundHeight(void* context, float x, float y, float top, float bottom, float& height);

// The world's camera and viewport as the opaque models leave them, in pixels of a target of this size;
// cleared once the frame is done (Engine/Source/Lumenveil/Lumenveil.cpp).
void CaptureOpaqueState(uint32_t targetWidth, uint32_t targetHeight);
bool HasOpaqueState();
bool OpaqueViewport(D3DVIEWPORT9& out);
void ClearOpaqueState();

// The opaque state, then ReadClientWorld.
bool BuildFrameInputs(FrameInputs& out);

// Everything but the matrices and the viewport, from the client's own state (WoW/Source/Ui/LumenveilInputs.cpp):
// the camera, the clock, the sun and moon, the weather, the zone's light and fog, the map and zone, the player
// and the units near the camera. False when the world is not in a state to read.
bool ReadClientWorld(FrameInputs& out);
}
