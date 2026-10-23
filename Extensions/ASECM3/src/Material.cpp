#include "ASECM3/Material.h"
#include "ASECM3/BufferSurface.h"
#include <utility>
namespace ASECM3 {
const Property* MaterialRecord::Find(std::string_view name) const {
    for (const auto& value:properties) if (value.name==name) return &value;
    return nullptr;
}
namespace {
struct Budget {
    uint64_t fields=0, bytes=0;
    void Add(uint64_t count,uint64_t size) {
        fields+=count; bytes+=size;
        if (fields>2000000 || bytes>128*1024*1024) throw Error("M3 material metadata budget exceeded");
    }
};
MaterialRecord Capture(const Document& doc,const Record& source,Budget& budget) {
    MaterialRecord out; const auto& layout=source.Schema();
    budget.Add(layout.fieldCount,layout.size);
    out.tag=layout.name; out.version=layout.version;
    out.bytes=source.Bytes();
    for (uint32_t i=0;i<layout.fieldCount;++i) {
        const auto& field=layout.fields[i]; const std::string kind=field.kind;
        if (kind=="record" || kind=="bytes") continue; // nested leaves follow
        if (kind=="reference") {
            const auto ref=source.Ref(field.name);
            std::string tag=field.referenceTag;
            if (tag.empty() && ref.count) tag=doc.Resolve(ref).tag;
            out.references.push_back({field.name,std::move(tag),ref});
            continue;
        }
        Property value; value.name=field.name; value.kind=kind;
        if (kind=="float") value.value=source.Float(field.name);
        else if (kind=="int16" || kind=="int32") value.value=int64_t(source.Int(field.name));
        else if (kind=="uint64") value.value=source.UInt64(field.name);
        else if (kind=="uint8" || kind=="uint16" || kind=="uint32") value.value=uint64_t(source.UInt(field.name));
        else throw Error("Unsupported material scalar kind: "+kind);
        out.properties.push_back(std::move(value));
    }
    return out;
}
struct MaterialType { const char* field; const char* tag; };
constexpr MaterialType Types[] = {
    {nullptr,nullptr}, {"materials_standard","MAT_"}, {"materials_displacement","DIS_"},
    {"materials_composite","CMP_"}, {"materials_terrain","TER_"}, {"materials_volume","VOL_"},
    {"materials_hair","HAI_"}, {"materials_creep","CREP"}, {"materials_volumenoise","VON_"},
    {"materials_splatterrainbake","STBM"}, {"materials_reflection","REF_"},
    {"materials_lensflare","LFLR"}, {"materials_buffer","MADD"}
};
void CheckCompositeGraph(const MaterialCatalog& catalog) {
    std::vector<uint8_t> visited(catalog.materials.size(),0);
    struct Cursor { uint32_t material; size_t child=0; };
    std::vector<Cursor> stack;
    for (uint32_t root=0;root<catalog.materials.size();++root) {
        if (visited[root]) continue;
        visited[root]=1; stack.push_back({root,0});
        while (!stack.empty()) {
            auto& cursor=stack.back(); const auto& edges=catalog.materials[cursor.material].composite;
            if (cursor.child==edges.size()) { visited[cursor.material]=2; stack.pop_back(); continue; }
            const auto child=edges[cursor.child++].materialReference;
            if (child>=catalog.materials.size()) throw Error("M3 composite references a missing material");
            if (visited[child]==1) throw Error("M3 composite material graph contains a cycle");
            if (!visited[child]) { visited[child]=1; stack.push_back({child,0}); }
        }
    }
}
}
MaterialCatalog DecodeMaterials(const Document& doc,const ModelData& mesh) {
    MaterialCatalog out; const auto model=doc.Model();
    Budget budget;
    const auto references=model.Ref("material_references");
    if (references.count>65536) throw Error("M3 material count exceeds native material budget");
    out.materials.reserve(references.count);
    for (uint32_t i=0;i<references.count;++i) {
        const auto reference=doc.At(references,i);
        Material material; material.type=reference.UInt("type"); material.sourceIndex=reference.UInt("material_index");
        if (!material.type || material.type>=sizeof(Types)/sizeof(Types[0]))
            throw Error("Unknown M3 material reference type "+std::to_string(material.type));
        const auto type=Types[material.type];
        if (!model.Has(type.field)) throw Error("M3 material type is absent in this model version");
        const auto array=model.Ref(type.field);
        if (material.sourceIndex>=array.count) throw Error("M3 material reference index exceeds its type array");
        doc.Resolve(array,type.tag);
        const auto record=doc.At(array,material.sourceIndex);
        material.name=record.Has("name")?record.String("name"):std::string();
        material.record=Capture(doc,record,budget);
        for (const auto& ref:material.record.references) {
            if (ref.tag!="LAYR" || !ref.value.count) continue;
            doc.Resolve(ref.value,"LAYR");
            for (uint32_t layerIndex=0;layerIndex<ref.value.count;++layerIndex) {
                const auto layer=doc.At(ref.value,layerIndex);
                MaterialLayer value;
                value.role=ref.name; value.sourceSection=ref.value.section; value.sourceIndex=layerIndex;
                value.image=layer.String("color_bitmap"); value.record=Capture(doc,layer,budget);
                material.layers.push_back(std::move(value));
            }
        }
        if (material.type==3) {
            const auto sections=record.Ref("sections");
            if (sections.count) doc.Resolve(sections,"CMS_");
            for (uint32_t j=0;j<sections.count;++j) {
                const auto section=doc.At(sections,j);
                material.composite.push_back({section.UInt("material_reference_index"),Capture(doc,section,budget)});
            }
        }
        if (material.type==12) {
            const auto paths=record.Ref("texture_paths");
            if (paths.count) doc.Resolve(paths,"SCHR");
            for (uint32_t j=0;j<paths.count;++j) material.bufferTextures.push_back(doc.At(paths,j).String("path"));
            for (const auto* field:{"unknown2","unknown3","unknown4"}) if (record.Has(field)) {
                const auto ref=record.Ref(field); if (!ref.count) continue;
                const auto& section=doc.Resolve(ref);
                if (!section.layout) throw Error("Unknown M3 buffer payload layout");
                const uint64_t size=uint64_t(ref.count)*section.layout->size;
                budget.Add(0,size);
                const auto* begin=doc.Data(section.offset,static_cast<size_t>(size));
                material.bufferPayloads.push_back({field,section.tag,ref.count,section.version,std::vector<uint8_t>(begin,begin+size)});
            }
            // Keep unrecognized buffer layouts available for future decoding.
            // A failed structural decode does not turn opaque data into diffuse.
            const Material::BufferPayload* ids=nullptr;
            const Material::BufferPayload* data=nullptr;
            for(const auto& payload:material.bufferPayloads) {
                if(payload.role=="unknown2")ids=&payload;
                if(payload.role=="unknown4")data=&payload;
            }
            if(ids && data) {
                std::vector<uint32_t> expected;
                for(size_t at=0;at+4<=ids->bytes.size();at+=4) {
                    uint32_t id=0;for(unsigned b=0;b<4;++b)id|=uint32_t(ids->bytes[at+b])<<(b*8);
                    expected.push_back(id);
                }
                try { material.bufferParameters=DecodeBufferMaterial(data->bytes,expected); }
                catch(const Error& e) { material.bufferDecodeError=e.what(); }
            } else material.bufferDecodeError="MADD indexed parameter payload is absent";
            BuildBufferSurface(material);
        }
        out.materials.push_back(std::move(material));
    }
    CheckCompositeGraph(out);
    const auto divisions=model.Ref("divisions");
    if (divisions.count!=mesh.divisions.size()) throw Error("M3 material/mesh division mismatch");
    for (uint32_t division=0;division<divisions.count;++division) {
        const auto batches=doc.At(divisions,division).Ref("batches");
        if (out.batches.size()+uint64_t(batches.count)>1000000) throw Error("M3 draw batch budget exceeded");
        if (batches.count) doc.Resolve(batches,"BAT_");
        for (uint32_t i=0;i<batches.count;++i) {
            const auto record=doc.At(batches,i);
            MaterialBatch batch;
            batch.division=division; batch.index=i;
            batch.region=record.UInt("region_index"); batch.material=record.UInt("material_reference_index");
            batch.flags=record.UInt("flags"); batch.priorityPlane=record.UInt("priority_plane");
            batch.boundsIndex=record.UInt("bounds_index"); batch.colorIndex=record.UInt("color_index"); batch.bone=record.Int("bone");
            if (batch.region>=mesh.divisions[division].regions.size()) throw Error("M3 batch references a missing region");
            if (batch.material>=out.materials.size()) throw Error("M3 batch references a missing material");
            if (batch.bone < -1 || batch.bone>=static_cast<int64_t>(mesh.skeleton.size())) throw Error("M3 batch references a missing bone");
            out.batches.push_back(batch);
        }
    }
    return out;
}
}
