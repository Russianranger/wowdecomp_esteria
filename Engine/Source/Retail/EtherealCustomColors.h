// Generated from the Custom texture manifest and custom_lightning_palettes.json.
#ifndef ENGINE_SOURCE_RETAIL_ETHEREALCUSTOMCOLORS_H
#define ENGINE_SOURCE_RETAIL_ETHEREALCUSTOMCOLORS_H
#include "Engine/Source/Retail/EtherealEffects.h"
#include <iterator>
#include <string>

namespace Retail::Ethereal {
// Private texture IDs resolve to local MPQ assets, never to Retail CDN files.
inline constexpr uint32_t LocalTexture = 0xf0038000u, CustomColors = 10;
inline constexpr const char* CustomColorNames[] = {
    "Sunforged",
    "Jade Flame",
    "Blood Moon",
    "Glacier",
    "Rose Quartz",
    "Cobalt Storm",
    "Verdigris",
    "Duskfire",
    "Moonstone",
    "Chartreuse",
};
inline constexpr EffectPalette CustomEffects[] = {
    {0xf0039000u, "Sunforged", {0xffa27f2eu,0xff66351bu,0xffa69d83u},
        {0xffe8b642u,0xff9d522au,0xfffff1c9u}, {0xff3f2111u,0xff514017u,0xff403c32u}},
    {0xf0039001u, "Jade Flame", {0xff3c9b55u,0xff0e3c41u,0xff8ca694u},
        {0xff55de79u,0xff165d64u,0xffd7ffe3u}, {0xff092528u,0xff1e4e2au,0xff364039u}},
    {0xf0039002u, "Blood Moon", {0xff983448u,0xff310e31u,0xffa6878cu},
        {0xffd94b67u,0xff4c164cu,0xffffd0d7u}, {0xff1e091eu,0xff4c1a24u,0xff403436u}},
    {0xf0039003u, "Glacier", {0xff579ca9u,0xff1f396du,0xff9aa4a6u},
        {0xff7cdff2u,0xff3058a8u,0xffedfdffu}, {0xff132343u,0xff2b4e55u,0xff3b3f40u}},
    {0xf0039004u, "Rose Quartz", {0xffa56c84u,0xff603854u,0xffa69ca2u},
        {0xffec9abdu,0xff945682u,0xfffff0fau}, {0xff3b2234u,0xff533642u,0xff403c3eu}},
    {0xf0039005u, "Cobalt Storm", {0xff3952a6u,0xff182248u,0xff8c9aa6u},
        {0xff5275edu,0xff25346fu,0xffd7edffu}, {0xff0f152cu,0xff1d2953u,0xff363b40u}},
    {0xf0039006u, "Verdigris", {0xff31867bu,0xff653a23u,0xff7fa69fu},
        {0xff46bfb0u,0xff9c5a36u,0xffc4fff4u}, {0xff3e2416u,0xff18433eu,0xff31403du}},
    {0xf0039007u, "Duskfire", {0xffa66038u,0xff54337bu,0xffa69478u},
        {0xffed8950u,0xff814fbdu,0xffffe4b8u}, {0xff34204cu,0xff53301cu,0xff40392eu}},
    {0xf0039008u, "Moonstone", {0xff808c9bu,0xff423e62u,0xffa1a3a6u},
        {0xffb7c8ddu,0xff665f96u,0xfff7fbffu}, {0xff29263cu,0xff40464du,0xff3e3f40u}},
    {0xf0039009u, "Chartreuse", {0xff81983au,0xff2f4d3cu,0xff9ea687u},
        {0xffb8d953u,0xff49775cu,0xfff3ffd0u}, {0xff1d3025u,0xff404c1du,0xff3d4034u}},
};
static_assert(std::size(CustomColorNames) == CustomColors && std::size(CustomEffects) == CustomColors);
inline uint32_t EffectCount() { return uint32_t(std::size(Effects) + std::size(CustomEffects)); }
inline const EffectPalette& Effect(uint32_t index) {
    return index < std::size(Effects) ? Effects[index] : CustomEffects[index - std::size(Effects)];
}
inline std::string TexturePath(uint32_t id) {
    return id >= LocalTexture && id < LocalTexture + 4 * CustomColors
        ? "custom/ethereal/colors/" + std::to_string(id - LocalTexture) + ".blp" : std::string{};
}
}
#endif
