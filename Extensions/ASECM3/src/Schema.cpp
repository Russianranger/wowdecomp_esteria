#include "ASECM3/Document.h"
#include <iterator>
namespace ASECM3 {
#include "Schema.generated.inc"
const Field* Layout::Find(std::string_view key) const {
    for (uint32_t i=0;i<fieldCount;++i) if (key == fields[i].name) return fields+i;
    return nullptr;
}
const Layout* FindLayout(std::string_view tag, uint32_t version, uint32_t mdVersion) {
    for (const auto& layout : kLayouts)
        if (tag == layout.name && version == layout.version && mdVersion == layout.mdVersion) return &layout;
    return nullptr;
}
}
