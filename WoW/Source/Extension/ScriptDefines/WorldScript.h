#ifndef WOW_SOURCE_EXTENSION_SCRIPTDEFINES_WORLDSCRIPT_H
#define WOW_SOURCE_EXTENSION_SCRIPTDEFINES_WORLDSCRIPT_H

#include <cstdint>
#include "WoW/Source/Extension/ScriptObject.h"

// The world scene: map boundaries and the world tick. Map hooks fire from
// WoW/Source/WorldClient/World.cpp, the tick from WoW/Source/Ui/WorldFrame.cpp. These are map
// boundaries as opposed to ClientScript::OnEnterWorld/OnLeaveWorld, which are session boundaries:
// a server transfer (SMSG_NEW_WORLD) unloads and loads a map without leaving the game.
class WorldScript : public ScriptObject {
    protected:
    explicit WorldScript(const char* name);

    public:
    // End of CWorld::LoadMap(): the map (CMap::Load), its area of interest, the liquid material
    // settings and the clip planes are set up. mapId is the Map.dbc id, mapName its directory
    // name (Map.dbc m_directory). Fires on world entry and on every map transfer.
    virtual void OnMapLoaded(uint32_t mapId, const char* mapName) {}

    // World::UnloadMap(), before CMap::Unload(): the fade-out list is already cleared, the map and
    // its objects are still loaded. Fires only when a map was active (the call from a session that
    // never loaded one does not reach it).
    virtual void OnMapUnload() {}

    // End of CGWorldFrame::OnWorldUpdate(): the camera, the active mover, the visible objects,
    // CWorld::Update (area of interest, day/night, fog) and the sound listener are updated for
    // this frame and the world is about to be rendered. elapsedSec is the world frame's time.
    // Fires only while the world frame is active (in the world, not in the glue).
    virtual void OnWorldUpdate(float elapsedSec) {}
};

#endif
