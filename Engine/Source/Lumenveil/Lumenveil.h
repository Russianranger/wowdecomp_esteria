#ifndef ENGINE_SOURCE_LUMENVEIL_LUMENVEIL_H
#define ENGINE_SOURCE_LUMENVEIL_LUMENVEIL_H

// Not in the original 3.3.5a binary. Lumenveil (https://github.com/lumenveil/Lumenveil, GPL-2.0; a modified
// CoAVolFog) adds volumetric fog, light shafts, night lights and water to the world. The stock-client mod wraps
// Direct3D 9 and patches four calls in CGWorldFrame::OnWorldRender; this port is called from those same places in
// the source (WoW/Source/Ui/WorldFrame.cpp) and draws through GxEffects (Engine/Source/Gx/GxEffects.h), so it runs
// on the backends that have it: Vulkan.
//
// Its settings are the mod's own files beside the executable, read at start and again whenever they change:
// CoAVolFog.ini (every effect) and CoAVolFog-regions.ini (per zone, hour and weather). The log is
// Logs\Lumenveil.log. The cvar lumenveil (1) switches it all off and on.
//
// The ported parts live in Core/ (see Core/README.md); this file and Lumenveil.cpp replace the mod's hooks.cpp and
// dllmain.cpp.
class Lumenveil {
    public:
    // At client start: the cvars, the log, the settings and the zone looks.
    static void Startup();

    // CGWorldFrame::OnWorldRender, in order. FrameBegin/FrameEnd bracket the whole world draw; OpaqueBegin/Done the
    // opaque models (M2PASS_0); LiquidBegin/End the liquid pass; WorldDone comes once the world is drawn, before
    // the full-screen effects (FFX::Finish).
    static void FrameBegin();
    static void OpaqueBegin();
    static void OpaqueDone();
    static void LiquidBegin();
    // Before each liquid instance is drawn: magma (lava and slime) is left as the client draws it, depth writes included.
    static void LiquidInstance(bool magma);
    static void LiquidEnd();
    static void WorldDone();
    static void FrameEnd();
};

#endif
