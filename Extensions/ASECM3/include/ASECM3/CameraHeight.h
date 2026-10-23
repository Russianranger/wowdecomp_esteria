#pragma once
#include "Scene.h"
namespace ASECM3 {
float CameraPivotHeight(float lowerZ,float upperZ,float scale);
bool SceneCameraBounds(const Scene& scene,float& lowerZ,float& upperZ);
}
