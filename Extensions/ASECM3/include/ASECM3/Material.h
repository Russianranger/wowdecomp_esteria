#pragma once
#include "Document.h"
#include "BufferMaterial.h"
#include <variant>
#include <memory>
namespace ASECM3 {
// Retain typed source values, including nested animation-reference defaults,
// nulls, flags and IDs. No uint64 -> float loss or guessed material fallback.
struct Property {
    std::string name, kind;
    std::variant<uint64_t,int64_t,float> value;
};
struct NamedReference { std::string name, tag; Reference value; };
struct MaterialRecord {
    std::string tag;
    uint32_t version = 0;
    std::vector<uint8_t> bytes; // includes source-defined opaque fields/padding
    std::vector<Property> properties;
    std::vector<NamedReference> references;
    const Property* Find(std::string_view name) const;
};
struct MaterialLayer {
    std::string role, image;
    uint32_t sourceSection = 0, sourceIndex = 0;
    MaterialRecord record;
};
struct CompositeSection {
    uint32_t materialReference = 0;
    MaterialRecord record; // includes alpha_factor animation reference
};
struct Material {
    uint32_t type = 0, sourceIndex = 0;
    std::string name;
    MaterialRecord record;
    std::vector<MaterialLayer> layers;
    std::vector<CompositeSection> composite;
    // Node/buffer materials retain all texture paths, not a guessed diffuse.
    std::vector<std::string> bufferTextures;
    struct BufferPayload {
        std::string role, tag;
        uint32_t count = 0, version = 0;
        std::vector<uint8_t> bytes;
    };
    // Preserve original data alongside decoded indexed parameter storage.
    // Neither retention nor structural decoding means the graph is rendered.
    std::vector<BufferPayload> bufferPayloads;
    BufferMaterial bufferParameters;
    std::string bufferDecodeError;
    std::shared_ptr<const Material> bufferSurface;
    std::string bufferSurfaceError;
};
struct MaterialBatch {
    uint32_t division = 0, index = 0, region = 0, material = 0;
    uint32_t flags = 0, priorityPlane = 0, boundsIndex = 0, colorIndex = 0;
    int32_t bone = -1;
};
struct MaterialCatalog {
    std::vector<Material> materials; // indexed by MATM, not underlying MAT_ index
    std::vector<MaterialBatch> batches;
};
MaterialCatalog DecodeMaterials(const Document& document, const ModelData& model);
}
