#pragma once
namespace ASECM3 {
struct Material;
// Build a named-input surface for the existing preview lighting path. Original
// MADD records remain intact; this does not claim source lighting/ECSpec parity.
void BuildBufferSurface(Material& material);
}
