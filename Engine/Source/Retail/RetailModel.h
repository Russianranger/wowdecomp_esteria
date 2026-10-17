#ifndef ENGINE_SOURCE_RETAIL_RETAILMODEL_H
#define ENGINE_SOURCE_RETAIL_RETAILMODEL_H

#include <cstdint>
#include <string>
#include <vector>

namespace Retail {

struct AnimationFile {
    uint16_t id;
    uint16_t variation;
    uint32_t fileId;
};

struct ModelFiles {
    uint32_t version = 0;
    uint32_t skeleton = 0;
    std::vector<uint32_t> skins;
    std::vector<uint32_t> textures;
    std::vector<AnimationFile> animations;
};

// All offsets remain file-relative. Preparation happens before the existing M2 relocation
// pass; cached CDN bytes are never changed. Unsupported features fail with a reason.
bool InspectModel(const std::vector<uint8_t>& bytes, ModelFiles& files, std::string& error);
bool PrepareModel(const std::vector<uint8_t>& bytes, std::vector<uint8_t>& prepared,
    ModelFiles& files, std::string& error);
bool ValidateSkin(const std::vector<uint8_t>& bytes, const std::vector<uint8_t>& model,
    std::string& error);
bool PrepareSkin(std::vector<uint8_t>& bytes, const std::vector<uint8_t>& model,
    std::string& error);
// Legion+ chunked .anim files wrap the legacy payload in an AFM2 chunk; drop that 8-byte header in place.
bool StripAnimationChunk(std::vector<uint8_t>& bytes, std::string& error);
bool ValidateAnimation(const std::vector<uint8_t>& bytes, const std::vector<uint8_t>& model,
    uint16_t id, uint16_t variation, std::string& error);
std::string FileName(uint32_t fileId, const char* extension);

}
#endif
