#pragma once
#include <cstdint>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ASECM3 {
struct Error : std::runtime_error { using std::runtime_error::runtime_error; };
struct Field {
    const char* name;
    uint32_t offset, size;
    const char* kind;
    const char* referenceTag;
};
struct Layout {
    const char* name;
    uint32_t version, mdVersion, size;
    const Field* fields;
    uint32_t fieldCount;
    const Field* Find(std::string_view name) const;
};
const Layout* FindLayout(std::string_view tag, uint32_t version, uint32_t mdVersion);
struct Reference { uint32_t count = 0, section = 0, flags = 0; };
struct Section {
    std::string tag;
    uint32_t offset = 0, count = 0, version = 0;
    size_t span = 0;
    const Layout* layout = nullptr;
};
class Document;
class Record {
    const Document* m_document;
    uint32_t m_section, m_record;
    size_t Address(const Field& field) const;
public:
    Record(const Document& document, uint32_t section, uint32_t record);
    const Layout& Schema() const;
    bool Has(std::string_view field) const;
    uint32_t UInt(std::string_view field) const;
    uint64_t UInt64(std::string_view field) const;
    int32_t Int(std::string_view field) const;
    float Float(std::string_view field) const;
    Reference Ref(std::string_view field) const;
    std::string String(std::string_view field) const;
    std::vector<uint8_t> Bytes() const;
};
// Owns every input byte; no file-backed pointers or original-client state.
// Construct and decode on a worker before publishing a complete model instance.
class Document {
    std::vector<uint8_t> m_bytes;
    std::vector<Section> m_sections;
    uint32_t m_mdVersion = 0;
    Reference m_model;
    void ValidateReference(const Reference& ref, std::string_view expected) const;
    friend class Record;
public:
    static Document Parse(std::vector<uint8_t> bytes,bool validateReferences=true);
    void ValidateReferences()const;
    const std::vector<Section>& Sections() const { return m_sections; }
    uint32_t Version() const { return m_mdVersion; }
    Reference ModelReference() const { return m_model; }
    Record Model() const { return At(m_model, 0); }
    Record At(const Reference& ref, uint32_t record) const;
    const Section& Resolve(const Reference& ref, std::string_view expected = {}) const;
    std::string String(const Reference& ref) const;
    uint32_t U32(size_t offset) const;
    uint16_t U16(size_t offset) const;
    uint8_t U8(size_t offset) const;
    float F32(size_t offset) const;
    const uint8_t* Data(size_t offset, size_t length) const;
};
struct Vec3 { float x=0, y=0, z=0; };
struct Vertex {
    Vec3 position, normal;
    Vec3 tangent;
    uint8_t color[4]={255,255,255,255}; // RGBA; source COL bytes are BGRA
    uint8_t tangentSign=255;
    bool hasTangent=false,hasColor=false,hasTangentSign=false;
    uint8_t weights[4] = {}, boneLookup[4] = {};
    std::vector<float> uv; // source packed UV / 32768, before per-region transform
};
struct AnimatedValue {
    uint32_t id=0,interpolation=0;
    float value[4]={};
};
struct Bone {
    std::string name;
    int32_t parent=-1;
    uint32_t flags=0;
    AnimatedValue translation,rotation,scale;
    float inverseBind[16]={};
};
struct Animation { std::string name; uint32_t start=0,end=0,flags=0,blendMs=0; };
struct Region {
    uint32_t firstVertex=0,vertexCount=0,firstIndex=0,indexCount=0;
    uint32_t firstBoneLookup=0,boneLookupCount=0,flags=0;
    float uvMultiply=16,uvOffset=0;
};
struct Division { std::vector<uint16_t> indices; std::vector<Region> regions; };
struct ModelData {
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<Division> divisions;
    std::vector<Animation> animations;
    std::vector<Bone> skeleton;
    std::vector<uint16_t> bonePalette;
    uint32_t bones=0,materials=0,particles=0,ribbons=0;
    Vec3 lower,upper;
};
ModelData DecodeModel(const Document& document);
} // namespace ASECM3
