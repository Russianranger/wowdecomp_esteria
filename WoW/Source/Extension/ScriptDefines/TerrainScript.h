#ifndef WOW_SOURCE_EXTENSION_SCRIPTDEFINES_TERRAINSCRIPT_H
#define WOW_SOURCE_EXTENSION_SCRIPTDEFINES_TERRAINSCRIPT_H

#include "WoW/Source/Extension/ScriptObject.h"

class CMapArea;

// Terrain tiles (WoW/Source/WorldClient/MapArea.cpp). A CMapArea is one ADT tile of the current
// map: CMap::PrepareUpdate creates the tiles of the area of interest and CMapArea::Load issues
// the asynchronous read of World\Maps\<map>\<map>_<x>_<y>.adt; the completion callback parses
// the file (ProcessIffChunks: header, chunk table, texture names, doodad and WMO placements,
// liquid data) and the 16x16 chunks are then built on demand (CMapArea::PrepareChunk). Tiles
// that leave the area of interest are purged. The map itself is a WorldScript matter
// (OnMapLoaded / OnMapUnload).
class TerrainScript : public ScriptObject {
    protected:
    explicit TerrainScript(const char* name);

    public:
    // CMapArea::AsyncCallback after ProcessIffChunks: the ADT is parsed, area.m_index is the
    // tile coordinate, m_doodadDef / m_mapObjDef with their counts list the placements, the
    // terrain textures are requested, m_liquid holds the MH2O data when present. No chunk is
    // built yet. Runs on the main thread from the async-read completion.
    virtual void OnTerrainTileLoaded(CMapArea& area) {}

    // Start of CMapArea::Purge: the tile is leaving the area of interest (or the map is being
    // unloaded); its chunks and file buffer are about to be released. A pending read is
    // cancelled, so a tile may be purged without ever having fired OnTerrainTileLoaded.
    virtual void OnTerrainTileUnload(CMapArea& area) {}
};

#endif
