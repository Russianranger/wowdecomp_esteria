#ifndef WOW_UI_BODY_SHAPE_SCRIPTS_H
#define WOW_UI_BODY_SHAPE_SCRIPTS_H

// Lua entry points for the body sliders. Included by both script tables (in-game UI and glue/creator), so each
// file registers the same names: SetBodyShape, GetBodyShape, GetBodyShapeNames, GetBodyShapeSupport,
// GetBodyShapeRange, ResetBodyShape, BodyShapeRandomize, BodyShapeProbe, BodyShapeBoneInfo, BodyShapeRegion,
// BodyShapeMeshStatus.

#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Retail/BodyShape.h"
#include "Engine/Source/Retail/BodyShapeMesh.h"

// Body sliders. Values run -1..1; 0 leaves the model untouched. They edit the creator preview or the local player.
static int32_t Script_SetBodyShape(lua_State* L) {
    if (!lua_isstring(L, 1) || !lua_isnumber(L, 2)) {
        luaL_error(L, "Usage: SetBodyShape(\"name\", value)");
        return 0;
    }

    lua_pushboolean(L, BodyShape::Set(lua_tolstring(L, 1, nullptr), static_cast<float>(lua_tonumber(L, 2))));
    return 1;
}

static int32_t Script_GetBodyShape(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: GetBodyShape(\"name\")");
        return 0;
    }

    lua_pushnumber(L, BodyShape::Get(lua_tolstring(L, 1, nullptr)));
    return 1;
}

// GetBodyShapeNames(["body" | "face"]) -> slider names, optionally one group only.
static int32_t Script_GetBodyShapeNames(lua_State* L) {
    const char* group = lua_isstring(L, 1) ? lua_tolstring(L, 1, nullptr) : nullptr;
    int32_t count = 0;

    for (const BodyShape::Slider& slider : BodyShape::Sliders()) {
        if (group && (slider.group == BodyShape::FaceGroup) != (strcmp(group, "face") == 0)) {
            continue;
        }

        lua_pushstring(L, slider.name);
        ++count;
    }

    return count;
}

// BodyShapeRegion("Brow") -> the model bone indices that face region moves (top-most bones only).
static int32_t Script_BodyShapeRegion(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        return 0;
    }

    CM2Model* model = BodyShape::Edited().model;

    if (!model) {
        return 0;
    }

    BodyShape::State& state = BodyShape::For(model);
    auto it = state.regions.find(lua_tolstring(L, 1, nullptr));

    if (it == state.regions.end()) {
        return 0;
    }

    for (const BodyShape::RegionBone& bone : it->second.bones) {
        lua_pushnumber(L, bone.index);
    }

    return static_cast<int32_t>(it->second.bones.size());
}

// GetBodyShapeSupport("name") -> true / false, or nil while no model has loaded yet.
static int32_t Script_GetBodyShapeSupport(lua_State* L) {
    int32_t support = lua_isstring(L, 1) ? BodyShape::Supported(lua_tolstring(L, 1, nullptr)) : -1;

    if (support < 0) {
        lua_pushnil(L);
    } else {
        lua_pushboolean(L, support);
    }

    return 1;
}

// GetBodyShapeRange("name") -> min, max.
static int32_t Script_GetBodyShapeRange(lua_State* L) {
    int32_t found = lua_isstring(L, 1) ? BodyShape::FindSlider(lua_tolstring(L, 1, nullptr)) : -1;

    if (found < 0) {
        return 0;
    }

    lua_pushnumber(L, BodyShape::Sliders()[found].minValue);
    lua_pushnumber(L, 1.0);
    return 2;
}

// BodyShapeMeshStatus() -> searched, profileName, vertexCount, active. For diagnosing the breast/butt sliders.
static int32_t Script_BodyShapeMeshStatus(lua_State* L) {
    CM2Model* model = BodyShape::Edited().model;
    BodyShape::State empty;
    BodyShape::State& state = model ? BodyShape::For(model) : empty;
    lua_pushboolean(L, state.meshSearched);
    lua_pushstring(L, BodyShapeMesh::Name(state.meshProfile));
    lua_pushnumber(L, static_cast<double>(state.bind.size() / 6));
    lua_pushboolean(L, model && model->m_bodyShapeVertices != nullptr);
    return 4;
}

static int32_t Script_ResetBodyShape(lua_State* L) {
    BodyShape::Reset();
    return 0;
}

// BodyShapeProbe(index, mirror, sx, sy, sz, tx, ty, tz): scale and move one raw bone (and its mirror partner).
// index < 0 turns the probe off.
static int32_t Script_BodyShapeProbe(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: BodyShapeProbe(index, mirror, sx, sy, sz, tx, ty, tz)");
        return 0;
    }

    float scale[3] = { 1.0f, 1.0f, 1.0f };
    float move[3] = { 0.0f, 0.0f, 0.0f };

    for (int32_t i = 0; i < 3; ++i) {
        if (lua_isnumber(L, 3 + i)) {
            scale[i] = static_cast<float>(lua_tonumber(L, 3 + i));
        }
        if (lua_isnumber(L, 6 + i)) {
            move[i] = static_cast<float>(lua_tonumber(L, 6 + i));
        }
    }

    BodyShape::SetProbe(static_cast<int32_t>(lua_tonumber(L, 1)), lua_toboolean(L, 2) != 0, scale, move);
    return 0;
}

// BodyShapeBoneInfo(index) -> keyId, parentIndex, x, y, z; with no argument -> boneCount. Nothing past the end.
static int32_t Script_BodyShapeBoneInfo(lua_State* L) {
    CM2Model* model = BodyShape::Edited().model;

    if (!model || !model->IsLoaded()) {
        return 0;
    }

    if (!lua_isnumber(L, 1)) {
        lua_pushnumber(L, model->GetBoneCount());
        return 1;
    }

    int32_t key, parent;
    C3Vector pivot;

    if (!model->GetBoneInfoByIndex(static_cast<uint32_t>(lua_tonumber(L, 1)), key, parent, pivot)) {
        return 0;
    }

    lua_pushnumber(L, key);
    lua_pushnumber(L, parent);
    lua_pushnumber(L, pivot.x);
    lua_pushnumber(L, pivot.y);
    lua_pushnumber(L, pivot.z);
    return 5;
}

// BodyShapeLog("text"): appends a line to Logs\BodyShape.log, so the creator Lua can report what it sees.
static int32_t Script_BodyShapeLog(lua_State* L) {
    if (lua_isstring(L, 1)) {
        BodyShape::Log(lua_tolstring(L, 1, nullptr));
    }

    return 0;
}

// BodyShapeRandomize(): random values for every slider the current model supports.
static int32_t Script_BodyShapeRandomize(lua_State* L) {
    BodyShape::Randomize();
    return 0;
}

#define BODY_SHAPE_SCRIPT_ENTRIES \
    { "SetBodyShape", &Script_SetBodyShape }, \
    { "GetBodyShape", &Script_GetBodyShape }, \
    { "GetBodyShapeNames", &Script_GetBodyShapeNames }, \
    { "ResetBodyShape", &Script_ResetBodyShape }, \
    { "BodyShapeRandomize", &Script_BodyShapeRandomize }, \
    { "GetBodyShapeSupport", &Script_GetBodyShapeSupport }, \
    { "BodyShapeRegion", &Script_BodyShapeRegion }, \
    { "GetBodyShapeRange", &Script_GetBodyShapeRange }, \
    { "BodyShapeMeshStatus", &Script_BodyShapeMeshStatus }, \
    { "BodyShapeProbe", &Script_BodyShapeProbe }, \
    { "BodyShapeBoneInfo", &Script_BodyShapeBoneInfo }, \
    { "BodyShapeLog", &Script_BodyShapeLog },

#endif
