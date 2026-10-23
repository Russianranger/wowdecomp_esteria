#pragma once
#include "Document.h"
#include <array>
namespace ASECM3 {
struct ClothTether {uint32_t particle,anchor;double maximumLength;};
// Shortest structural-mesh path to a fixed vertex; no straight-line shortcut
// across folds/holes. Components without a pin intentionally receive no tether.
std::vector<ClothTether> BuildClothTethers(const std::vector<Vec3>& points,
    const std::vector<std::array<uint32_t,2>>& edges,const std::vector<uint8_t>& pinned);
}
