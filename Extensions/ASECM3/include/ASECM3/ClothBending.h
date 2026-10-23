#pragma once
#include "Document.h"
#include <array>
namespace ASECM3 {
// Order: shared-edge endpoints, first opposite vertex, second opposite vertex.
// Signed angle preserves the authored fold side. Degenerate triangles return
// false; malformed finite-domain inputs report an Error.
bool ClothBendGeometry(const std::array<Vec3,4>& points,double& angle,std::array<Vec3,4>* gradient=nullptr);
bool ProjectClothBend(std::array<Vec3,4>& points,const std::array<double,4>& inverseMass,double restAngle,double stiffness);
}
