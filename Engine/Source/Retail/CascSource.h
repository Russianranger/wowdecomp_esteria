#ifndef ENGINE_SOURCE_RETAIL_CASCSOURCE_H
#define ENGINE_SOURCE_RETAIL_CASCSOURCE_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Retail {

struct HandItemAppearance {
    uint32_t modelId = 0;
    uint32_t textureId = 0; // Replaceable object-skin texture (type 2).
};

// One source per pinned build. Storage calls and dependency publication are serialized.
// Only retail/ references and explicit aliases are handled; stock paths stay in MPQ.
class CascSource {
public:
    CascSource();
    ~CascSource();
    CascSource(const CascSource&) = delete;
    CascSource& operator=(const CascSource&) = delete;

    bool Configure(const char* configPath, std::string& error);
    bool Handles(const char* name);
    bool Exists(const char* name, std::string& error);
    bool Read(const char* name, std::vector<uint8_t>& bytes, std::string& error);
    bool Inspect(const char* name, std::vector<uint8_t>& bytes, std::string& error);
    void ReleaseModel(const char* name);
    bool CachedSize(const char* name, uint32_t& size);
    bool HandItem(uint32_t displayId, HandItemAppearance& appearance, bool mainHand = false);
    bool SetHandItem(uint32_t displayId, HandItemAppearance appearance);
    bool SetMainHandPreview(HandItemAppearance appearance);
    bool HasClientTable(const char* name);
    bool MergeTable(const char* name, const std::vector<uint8_t>& base,
        std::vector<uint8_t>& merged, std::string& error);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// Lazy process-wide source, configured by WOW_RETAIL_CONFIG or RetailCasc.ini in the
// working directory. Initialization does no network IO until a Retail reference is read.
CascSource& Source();

}
#endif
