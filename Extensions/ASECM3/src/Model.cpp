#include "ASECM3/Document.h"
#include <algorithm>
#include <cmath>
namespace ASECM3 {
namespace {
struct VertexLayout {
    uint32_t stride=0,position=~0u,normal=~0u,packedNormal=~0u,weights=~0u,lookup=~0u,pairs=0;
    uint32_t tangent=~0u,packedTangent=~0u,color=~0u;
    std::vector<uint32_t> packedUV,floatUV;
};
VertexLayout DescribeVertices(uint32_t flags) {
    if (flags&0x80000000u) throw Error("Unsupported high-bit M3 vertex layout");
    VertexLayout l;
    auto add=[&](uint32_t count) { auto start=l.stride; l.stride+=count; return start; };
    if (flags&1) l.position=add(12);
    l.pairs=(flags&0x20?2:0)+(flags&0x40?2:0);
    if (l.pairs) { l.weights=add(l.pairs); l.lookup=add(l.pairs); }
    if (flags&0x80) l.normal=add(12);
    if (flags&0x800000) l.packedNormal=add(4);
    for (auto bit : {0x100u,0x200u,0x400u,0x800u,0x1000u}) if (flags&bit) {const auto offset=add(4);if(bit==0x200)l.color=offset;}
    for (auto bit : {0x2000u,0x4000u,0x8000u,0x10000u}) if (flags&bit) l.floatUV.push_back(add(8));
    for (auto bit : {0x20000u,0x40000u,0x80000u,0x100000u,0x40000000u}) if (flags&bit) l.packedUV.push_back(add(4));
    if (flags&0x200000) l.normal=add(12);
    if (flags&0x400000) l.tangent=add(12);
    if (flags&0x1000000) l.packedTangent=add(4);
    if (flags&0x2000000) add(4);
    if (flags&0x4000000) add(12);
    if (flags&0x8000000) add(12);
    if (flags&0x10000000) add(4);
    if (flags&0x20000000) add(4);
    if (l.position==~0u || !l.stride) throw Error("M3 mesh vertex layout has no position");
    return l;
}
uint32_t OptionalCount(const Record& model, const char* field) { return model.Has(field)?model.Ref(field).count:0; }
}
ModelData DecodeModel(const Document& doc) {
    ModelData out;
    const Record model=doc.Model();
    out.name=model.String("model_name");
    out.bones=OptionalCount(model,"bones"); out.materials=OptionalCount(model,"material_references");
    out.particles=OptionalCount(model,"particle_systems"); out.ribbons=OptionalCount(model,"ribbons");
    const auto bones=model.Ref("bones"), rests=model.Ref("bone_rests");
    if (bones.count>65536) throw Error("M3 skeleton exceeds its 16-bit palette address space");
    if (bones.count!=rests.count) throw Error("M3 BONE/IREF count mismatch");
    out.skeleton.reserve(bones.count);
    for (uint32_t i=0;i<bones.count;++i) {
        const auto record=doc.At(bones,i), rest=doc.At(rests,i);
        Bone bone; bone.name=record.String("name"); bone.parent=record.Int("parent"); bone.flags=record.UInt("flags");
        if (bone.parent<-1 || bone.parent>=static_cast<int32_t>(bones.count) || bone.parent==static_cast<int32_t>(i))
            throw Error("M3 bone parent is invalid: "+bone.name);
        auto readValue=[&](const char* field, AnimatedValue& value, int components) {
            const std::string key=field;
            value.id=record.UInt(key+".header.id"); value.interpolation=record.UInt(key+".header.interpolation");
            for (int c=0;c<components;++c) value.value[c]=record.Float(key+".default."+"xyzw"[c]);
        };
        readValue("location",bone.translation,3); readValue("rotation",bone.rotation,4); readValue("scale",bone.scale,3);
        float norm=0; for (auto v:bone.rotation.value) norm+=v*v;
        if (norm<0.00000001f) throw Error("M3 bone has a zero default quaternion: "+bone.name);
        for (int col=0;col<4;++col) for (int row=0;row<4;++row)
            bone.inverseBind[col*4+row]=rest.Float(std::string("matrix.")+"xyzw"[col]+"."+"xyzw"[row]);
        out.skeleton.push_back(std::move(bone));
    }
    // Check every parent chain without relying on source ordering.
    std::vector<uint8_t> visited(bones.count,0);
    for (uint32_t i=0;i<bones.count;++i) {
        int32_t cursor=i;
        while (cursor>=0 && visited[cursor]==0) { visited[cursor]=1; cursor=out.skeleton[cursor].parent; }
        if (cursor>=0 && visited[cursor]==1) throw Error("M3 skeleton contains a parent cycle");
        cursor=i;
        while (cursor>=0 && visited[cursor]==1) { visited[cursor]=2; cursor=out.skeleton[cursor].parent; }
    }
    const auto sequences=model.Ref("sequences");
    for (uint32_t i=0;i<sequences.count;++i) {
        const auto sequence=doc.At(sequences,i);
        Animation animation{sequence.String("name"),sequence.UInt("anim_ms_start"),sequence.UInt("anim_ms_end"),sequence.UInt("flags")};
        if(sequence.Has("ms_blend"))animation.blendMs=sequence.UInt("ms_blend");
        if (animation.end<animation.start) throw Error("M3 animation ends before it starts: "+animation.name);
        out.animations.push_back(std::move(animation));
    }
    const auto vertices=model.Ref("vertices");
    if (vertices.count) {
        const auto& section=doc.Resolve(vertices,"U8__");
        const auto layout=DescribeVertices(model.UInt("vertex_flags"));
        if (vertices.count%layout.stride) throw Error("M3 vertex byte count is not a multiple of its declared layout");
        const uint32_t count=vertices.count/layout.stride;
        if (count>10000000) throw Error("M3 mesh exceeds native vertex budget");
        out.vertices.reserve(count);
        for (uint32_t i=0;i<count;++i) {
            const size_t address=section.offset+size_t(i)*layout.stride;
            Vertex vertex;
            vertex.position={doc.F32(address+layout.position),doc.F32(address+layout.position+4),doc.F32(address+layout.position+8)};
            if (layout.packedNormal!=~0u) {
                const auto n=address+layout.packedNormal;
                vertex.normal={doc.U8(n)/127.5f-1,doc.U8(n+1)/127.5f-1,doc.U8(n+2)/127.5f-1};
                vertex.tangentSign=doc.U8(n+3);
                vertex.hasTangentSign=vertex.tangentSign==0||vertex.tangentSign==255;
            } else if (layout.normal!=~0u) {
                const auto n=address+layout.normal; vertex.normal={doc.F32(n),doc.F32(n+4),doc.F32(n+8)};
            }
            if(layout.packedTangent!=~0u){const auto t=address+layout.packedTangent;vertex.tangent={doc.U8(t)/127.5f-1,doc.U8(t+1)/127.5f-1,doc.U8(t+2)/127.5f-1};vertex.hasTangent=true;}
            else if(layout.tangent!=~0u){const auto t=address+layout.tangent;vertex.tangent={doc.F32(t),doc.F32(t+4),doc.F32(t+8)};vertex.hasTangent=true;}
            if(layout.color!=~0u){const auto c=address+layout.color;vertex.color[0]=doc.U8(c+2);vertex.color[1]=doc.U8(c+1);vertex.color[2]=doc.U8(c);vertex.color[3]=doc.U8(c+3);vertex.hasColor=true;}
            for (uint32_t j=0;j<layout.pairs;++j) { vertex.weights[j]=doc.U8(address+layout.weights+j); vertex.boneLookup[j]=doc.U8(address+layout.lookup+j); }
            for (auto offset:layout.floatUV) { vertex.uv.push_back(doc.F32(address+offset)); vertex.uv.push_back(doc.F32(address+offset+4)); }
            for (auto offset:layout.packedUV) { vertex.uv.push_back(static_cast<int16_t>(doc.U16(address+offset))/32768.0f); vertex.uv.push_back(static_cast<int16_t>(doc.U16(address+offset+2))/32768.0f); }
            if (!i) out.lower=out.upper=vertex.position;
            else {
                out.lower.x=std::min(out.lower.x,vertex.position.x); out.upper.x=std::max(out.upper.x,vertex.position.x);
                out.lower.y=std::min(out.lower.y,vertex.position.y); out.upper.y=std::max(out.upper.y,vertex.position.y);
                out.lower.z=std::min(out.lower.z,vertex.position.z); out.upper.z=std::max(out.upper.z,vertex.position.z);
            }
            out.vertices.push_back(std::move(vertex));
        }
    }
    const auto divisions=model.Ref("divisions"), boneLookup=model.Ref("bone_lookup");
    if (boneLookup.count) {
        const auto& lookup=doc.Resolve(boneLookup,"U16_");
        out.bonePalette.reserve(boneLookup.count);
        for (uint32_t i=0;i<boneLookup.count;++i) {
            const auto bone=doc.U16(lookup.offset+size_t(i)*2);
            if (bone>=bones.count) throw Error("M3 palette references a missing bone");
            out.bonePalette.push_back(bone);
        }
    }
    for (uint32_t d=0;d<divisions.count;++d) {
        const auto division=doc.At(divisions,d);
        Division mesh;
        const auto faces=division.Ref("faces"), regions=division.Ref("regions");
        if (faces.count) {
            const auto& section=doc.Resolve(faces,"U16_"); mesh.indices.reserve(faces.count);
            for (uint32_t i=0;i<faces.count;++i) mesh.indices.push_back(doc.U16(section.offset+size_t(i)*2));
        }
        for (uint32_t i=0;i<regions.count;++i) {
            const auto record=doc.At(regions,i);
            Region region;
            region.firstVertex=record.UInt("first_vertex_index"); region.vertexCount=record.UInt("vertex_count");
            region.firstIndex=record.UInt("first_face_index"); region.indexCount=record.UInt("face_count");
            region.firstBoneLookup=record.UInt("first_bone_lookup_index"); region.boneLookupCount=record.UInt("bone_lookup_count");
            if (record.Has("flags")) region.flags=record.UInt("flags");
            if (record.Has("uv_multiply")) { region.uvMultiply=record.Float("uv_multiply"); region.uvOffset=record.Float("uv_offset"); }
            if (uint64_t(region.firstVertex)+region.vertexCount>out.vertices.size()) throw Error("M3 region vertex range exceeds mesh");
            if (uint64_t(region.firstIndex)+region.indexCount>mesh.indices.size() || region.indexCount%3) throw Error("Invalid M3 region triangle range");
            if (uint64_t(region.firstBoneLookup)+region.boneLookupCount>boneLookup.count) throw Error("M3 region bone palette exceeds lookup array");
            for (size_t k=region.firstIndex;k<size_t(region.firstIndex)+region.indexCount;++k)
                if (mesh.indices[k]>=region.vertexCount) throw Error("M3 region contains an out-of-range local vertex index");
            for (size_t k=region.firstVertex;k<size_t(region.firstVertex)+region.vertexCount;++k)
                for (unsigned b=0;b<4;++b) if (out.vertices[k].weights[b] && out.vertices[k].boneLookup[b]>=region.boneLookupCount)
                    throw Error("M3 weighted vertex references an out-of-range region bone palette entry");
            mesh.regions.push_back(region);
        }
        out.divisions.push_back(std::move(mesh));
    }
    return out;
}
}
