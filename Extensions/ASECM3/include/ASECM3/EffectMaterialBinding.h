#pragma once
#include "Material.h"
#include "Animation.h"
namespace ASECM3 {
enum class MaterialRecordKind {Root,Layer,Composite};
struct MaterialRecordLocation {uint32_t material=0,index=0;MaterialRecordKind kind=MaterialRecordKind::Root;std::string role;};
struct EffectMaterialField {std::string name;uint32_t id=0,type=0;};
struct EffectMaterialLink {MaterialRecordLocation mesh,bank;std::vector<EffectMaterialField> fields;};
struct EffectMaterialBinding {std::vector<EffectMaterialLink> records;};
const MaterialRecord& LocateMaterialRecord(const MaterialCatalog& catalog,const MaterialRecordLocation& location);
const MaterialRecord& LocateMaterialRecord(const Material& material,const MaterialRecordLocation& location);
// Explicit auxiliary-bank lane only. Named material/type and record role are
// necessary but insufficient: the entire typed reference signature must match.
EffectMaterialBinding BindEffectMaterials(const MaterialCatalog& mesh,const MaterialCatalog& bank,const AnimationBank& animation);
// One source document establishes identity by exact record location. Duplicate
// names do not make a record ambiguous within that same owning catalog.
EffectMaterialBinding BindEmbeddedEffectMaterials(const MaterialCatalog& catalog,const AnimationBank& animation);
}
