#pragma once
#include "Document.h"
namespace ASECM3 {
// Optional <model.m3>.effectbank: magic line then one adjacent M3A basename.
// No directory scan, asset-family guess, script, or automatic phase activation.
std::string EffectDeclarationPath(const std::string& modelPath);
std::string ResolveEffectDeclaration(const std::string& modelPath,const std::vector<uint8_t>& declaration);
}
