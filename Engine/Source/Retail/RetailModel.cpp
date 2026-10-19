#include "Engine/Source/Retail/RetailModel.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <cmath>

namespace Retail {
namespace {

uint16_t U16(const std::vector<uint8_t>& b, size_t p) {
    return uint16_t(b[p]) | (uint16_t(b[p + 1]) << 8);
}
uint32_t U32(const std::vector<uint8_t>& b, size_t p) {
    return uint32_t(b[p]) | (uint32_t(b[p + 1]) << 8) |
        (uint32_t(b[p + 2]) << 16) | (uint32_t(b[p + 3]) << 24);
}
void Put(std::vector<uint8_t>& b, size_t p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) b[p + i] = uint8_t(v >> (8 * i));
}
bool Fail(std::string& error, const char* reason) { error = reason; return false; }
bool Range(const std::vector<uint8_t>& b, size_t p, size_t n) {
    return p <= b.size() && n <= b.size() - p;
}
bool Array(const std::vector<uint8_t>& b, size_t p, size_t stride) {
    return Range(b, p, 8) && U32(b, p + 4) <= b.size() &&
        U32(b, p) <= (b.size() - U32(b, p + 4)) / stride;
}
bool Index(const std::vector<uint8_t>& b, size_t table, uint32_t first, uint32_t count) {
    return first <= U32(b, table) && count <= U32(b, table) - first;
}
bool String(const std::vector<uint8_t>& b, size_t p) {
    if (!Array(b, p, 1)) return false;
    const uint32_t n = U32(b, p), start = U32(b, p + 4);
    return !n || b[start + n - 1] == 0;
}

struct Chunks {
    std::vector<uint8_t> body;
    ModelFiles files;
};

bool Parse(const std::vector<uint8_t>& b, Chunks& out, std::string& error) {
    out = {};
    if (!Range(b, 0, 8)) return Fail(error, "Truncated M2 header");
    if (U32(b, 0) == 0x3032444d) {
        out.body = b;
    } else {
        bool body = false, sfid = false, txid = false, skid = false, afid = false;
        for (size_t p = 0; p < b.size();) {
            if (!Range(b, p, 8)) return Fail(error, "Truncated M2 chunk header");
            const uint32_t tag = U32(b, p), size = U32(b, p + 4);
            p += 8;
            if (!Range(b, p, size)) return Fail(error, "M2 chunk exceeds file size");
            switch (tag) {
                case 0x3132444d: // MD21 contains an MD20 payload, offsets relative to payload
                    if (body) return Fail(error, "Duplicate MD21 chunk");
                    body = true;
                    out.body.assign(b.begin() + p, b.begin() + p + size);
                    break;
                case 0x44494653: // SFID includes ordinary skins followed by LOD skins
                case 0x44495854: { // TXID
                    bool& seen = tag == 0x44494653 ? sfid : txid;
                    if (seen || size % 4) return Fail(error, "Invalid or duplicate SFID/TXID chunk");
                    seen = true;
                    auto& ids = tag == 0x44494653 ? out.files.skins : out.files.textures;
                    for (size_t i = 0; i < size; i += 4) ids.push_back(U32(b, p + i));
                    break;
                }
                case 0x44494b53:
                    if (skid || size != 4) return Fail(error, "Invalid or duplicate SKID chunk");
                    skid = true;
                    out.files.skeleton = U32(b, p);
                    break;
                case 0x44494641:
                    if (afid || size % 8) return Fail(error, "Invalid or duplicate AFID chunk");
                    afid = true;
                    for (size_t i = 0; i < size; i += 8)
                        out.files.animations.push_back({U16(b, p + i), U16(b, p + i + 2), U32(b, p + i + 4)});
                    break;
                default:
                    break; // Unknown optional chunks are retained in the original CDN cache.
            }
            p += size;
        }
        if (!body) return Fail(error, "No MD21 model payload");
    }
    if (!Range(out.body, 0, 0x130) || U32(out.body, 0) != 0x3032444d)
        return Fail(error, "Invalid MD20 payload");
    out.files.version = U32(out.body, 4);
    return true;
}

// Validate tracks before handing bytes to the legacy relocation code, including the
// distinction between model-relative and external-animation-relative offsets.
thread_local std::string s_trackFailure;

bool TF(int tag) { s_trackFailure = "check " + std::to_string(tag); return false; }

bool Track(const std::vector<uint8_t>& b, size_t p, size_t keySize,
    const std::vector<uint8_t>* animation = nullptr, uint32_t sequence = 0) {
    if (!Range(b, p, keySize ? 20 : 12) || U16(b, p) > 3) return TF(1);
    const uint32_t loop = U16(b, p + 2), sequences = U32(b, 0x1c);
    if (loop != 0xffff && loop >= U32(b, 0x14)) return TF(2);
    if (!Array(b, p + 4, 8) || (keySize && !Array(b, p + 12, 8))) return TF(3);
    const uint32_t n = U32(b, p + 4);
    if (keySize && U32(b, p + 12) != n) return TF(4);
    if (loop == 0xffff && n > sequences) return TF(5);
    for (uint32_t i = 0; i < n; ++i) {
        size_t times = U32(b, p + 8) + size_t(i) * 8;
        size_t keys = keySize ? U32(b, p + 16) + size_t(i) * 8 : 0;
        if (keySize && U32(b, times) != U32(b, keys)) return TF(6);
        const bool external = loop == 0xffff && !(U32(b, U32(b, 0x20) + size_t(i) * 64 + 12) & 0x20);
        const auto* source = external ? (animation && i == sequence ? animation : nullptr) : &b;
        if (!source) continue;
        auto valid = [&](size_t pair, size_t stride) {
            const size_t offset = U32(b, pair + 4), count = U32(b, pair);
            const bool ok = offset <= source->size() && count <= (source->size() - offset) / stride;
            if (!ok) s_trackFailure = "offset " + std::to_string(offset) + " count " + std::to_string(count) + " stride " + std::to_string(stride) + " in " + std::to_string(source->size()) + " bytes";
            return ok;
        };
        if (!valid(times, 4) || (keySize && !valid(keys, keySize))) return TF(7);
        for (uint32_t j = 1; j < U32(b, times); ++j) {
            const size_t start = U32(b, times + 4);
            if (U32(*source, start + size_t(j - 1) * 4) > U32(*source, start + size_t(j) * 4)) { s_trackFailure = "order p=" + std::to_string(p) + " seq=" + std::to_string(i) + " j=" + std::to_string(j) + " start=" + std::to_string(start) + " n=" + std::to_string(U32(b, times)) + " a=" + std::to_string(U32(*source, start + size_t(j - 1) * 4)) + " b=" + std::to_string(U32(*source, start + size_t(j) * 4)); return false; }
        }
    }
    return true;
}

bool Tracks(const std::vector<uint8_t>& b, const std::vector<uint8_t>* anim = nullptr, uint32_t seq = 0,
    const std::vector<uint8_t>* bones = nullptr, const std::vector<uint8_t>* attachments = nullptr) {
    struct Group { size_t array, stride, track, key; };
    const Group groups[] = {
        {0x2c, 88, 16, 12}, {0x2c, 88, 36, 8}, {0x2c, 88, 56, 12},
        {0x48, 40, 0, 12}, {0x48, 40, 20, 2}, {0x58, 20, 0, 2},
        {0x60, 60, 0, 12}, {0x60, 60, 20, 16}, {0x60, 60, 40, 12},
        {0xf0, 40, 20, 1}, {0x100, 36, 24, 0},
        {0x108, 156, 16, 12}, {0x108, 156, 36, 4}, {0x108, 156, 56, 12},
        {0x108, 156, 76, 4}, {0x108, 156, 96, 4}, {0x108, 156, 116, 4}, {0x108, 156, 136, 1},
        {0x120, 176, 36, 12}, {0x120, 176, 56, 2}, {0x120, 176, 76, 4},
        {0x120, 176, 96, 4}, {0x120, 176, 132, 2}, {0x120, 176, 152, 1}
    };
    for (const auto& group : groups) {
        for (uint32_t i = 0; i < U32(b, group.array); ++i) {
            const size_t p = U32(b, group.array + 4) + size_t(i) * group.stride + group.track;
            // Hermite/Bezier values store value, in-tangent and out-tangent.
            size_t key = group.key;
            if (key && U16(b, p) >= 2) key *= 3;
            const auto* source = group.array == 0x2c && bones ? bones
                : group.array == 0xf0 && attachments ? attachments : anim;
            if (!Track(b, p, key, source, seq)) return false;
        }
    }
    return true;
}

// Retail cameras carry a spline FOV track. Constant tracks have an exact
// representation in the engine camera. No source file is rewritten.
bool Cameras(std::vector<uint8_t>& b, std::string& error) {
    if (!Array(b, 0x110, 116)) return Fail(error, "Invalid Retail camera array");
    std::vector<uint8_t> cameras;
    for (uint32_t i = 0; i < U32(b, 0x110); ++i) {
        const size_t p = U32(b, 0x114) + size_t(i) * 116, f = p + 96;
        if (!Track(b, p + 12, 36) || !Track(b, p + 44, 36) || !Track(b, p + 76, 12) || !Track(b, f, 12))
            return Fail(error, "Invalid Retail camera track");
        bool found = false; uint32_t fov = 0;
        for (uint32_t j = 0; j < U32(b, f + 12); ++j) {
            const size_t pair = U32(b, f + 16) + size_t(j) * 8;
            if (!Array(b, pair, 12)) return Fail(error, "External camera FOV is unsupported");
            for (uint32_t k = 0; k < U32(b, pair); ++k) {
                const size_t key = U32(b, pair + 4) + size_t(k) * 12;
                float value; const auto bits = U32(b, key); std::memcpy(&value, &bits, 4);
                if (!std::isfinite(value) || value <= 0 || value >= 3.142f ||
                    (found && fov != bits) || U32(b, key + 4) || U32(b, key + 8))
                    return Fail(error, "Animated Retail camera FOV is not implemented yet");
                fov = bits; found = true;
            }
        }
        if (!found) return Fail(error, "Retail camera has no FOV value");
        const size_t out = cameras.size(); cameras.resize(out + 100);
        std::copy_n(b.begin() + p, 4, cameras.begin() + out);
        Put(cameras, out + 4, fov);
        std::copy_n(b.begin() + p + 4, 92, cameras.begin() + out + 8);
    }
    if (!cameras.empty()) {
        while (b.size() % 4) b.push_back(0);
        Put(b, 0x114, uint32_t(b.size()));
        b.insert(b.end(), cameras.begin(), cameras.end());
    }
    return true;
}

// Shared skeletons (SKEL). The .skel chunks (SKS1 sequences, SKB1 bones, SKA1 attachments) hold the arrays an
// ordinary model keeps in its own body, with offsets relative to each chunk. Append each chunk to the body
// and rebase its offsets, so the rest of the pipeline sees an ordinary self-contained model.
struct SkelArray { uint32_t count, offset; };

bool SkelArrayAt(const uint8_t* chunk, size_t size, size_t at, size_t stride, SkelArray& out) {
    if (at + 8 > size) return false;
    std::memcpy(&out.count, chunk + at, 4);
    std::memcpy(&out.offset, chunk + at + 4, 4);
    return out.offset <= size && out.count <= (size - out.offset) / stride;
}

void SetHeaderArray(std::vector<uint8_t>& b, size_t field, uint32_t count, uint32_t offset) {
    Put(b, field, count);
    Put(b, field + 4, count ? offset : 0);
}

// Moves the offsets of one animation track (20 bytes) that lives in a skeleton chunk. Per-sequence arrays of
// sequences stored in external .anim files keep their anim-relative offsets.
bool RebaseTrack(std::vector<uint8_t>& b, size_t track, uint32_t base, size_t chunkEnd) {
    const uint32_t loop = U16(b, track + 2), count = U32(b, track + 4);
    if (loop == 0xffff && count > U32(b, 0x1c)) return false;
    if (!count) return true;
    for (const size_t field : {size_t(8), size_t(16)}) {
        const size_t pairs = size_t(base) + U32(b, track + field);
        if (pairs > chunkEnd || size_t(count) * 8 > chunkEnd - pairs) return false;
        for (uint32_t i = 0; i < count; ++i) {
            const size_t pair = pairs + size_t(i) * 8;
            const bool external = loop == 0xffff && i < U32(b, 0x1c) &&
                !(U32(b, U32(b, 0x20) + size_t(i) * 64 + 12) & 0x20);
            if (!external && U32(b, pair)) Put(b, pair + 4, U32(b, pair + 4) + base);
        }
        Put(b, track + field, uint32_t(pairs));
    }
    return true;
}

bool SpliceSkeleton(std::vector<uint8_t>& b, ModelFiles& files, const std::vector<uint8_t>& skel, std::string& error) {
    if (U32(b, 0x1c) || U32(b, 0x2c)) return Fail(error, "Model with a skeleton file also has its own bones or sequences");
    struct Chunk { const uint8_t* data = nullptr; size_t size = 0; };
    Chunk sks, skb, ska;
    for (size_t p = 0; p < skel.size();) {
        if (!Range(skel, p, 8)) return Fail(error, "Truncated SKEL chunk header");
        const uint32_t tag = U32(skel, p), size = U32(skel, p + 4);
        p += 8;
        if (!Range(skel, p, size)) return Fail(error, "SKEL chunk exceeds file size");
        switch (tag) {
            case 0x31534b53: sks = {skel.data() + p, size}; break; // SKS1
            case 0x31424b53: skb = {skel.data() + p, size}; break; // SKB1
            case 0x31414b53: ska = {skel.data() + p, size}; break; // SKA1
            case 0x44494641:                                       // AFID
                if (size % 8) return Fail(error, "Invalid SKEL AFID chunk");
                for (size_t i = 0; i < size; i += 8)
                    files.animations.push_back({U16(skel, p + i), U16(skel, p + i + 2), U32(skel, p + i + 4)});
                break;
            default: break;
        }
        p += size;
    }
    if (!sks.data || !skb.data) return Fail(error, "SKEL has no sequence or bone chunk");
    auto append = [&](const Chunk& chunk, uint32_t& base) {
        while (b.size() % 4) b.push_back(0);
        if (b.size() + chunk.size > 128 * 1024 * 1024) return false;
        base = uint32_t(b.size());
        b.insert(b.end(), chunk.data, chunk.data + chunk.size);
        return true;
    };
    uint32_t base = 0;
    SkelArray loops, sequences, lookups, bones, keys;
    if (!SkelArrayAt(sks.data, sks.size, 0, 4, loops) || !SkelArrayAt(sks.data, sks.size, 8, 64, sequences) ||
        !SkelArrayAt(sks.data, sks.size, 16, 2, lookups))
        return Fail(error, "Invalid SKEL sequence arrays");
    if (!append(sks, base)) return Fail(error, "Prepared model exceeds 128 MiB");
    SetHeaderArray(b, 0x14, loops.count, base + loops.offset);
    SetHeaderArray(b, 0x1c, sequences.count, base + sequences.offset);
    SetHeaderArray(b, 0x24, lookups.count, base + lookups.offset);

    if (!SkelArrayAt(skb.data, skb.size, 0, 88, bones) || !SkelArrayAt(skb.data, skb.size, 8, 2, keys))
        return Fail(error, "Invalid SKEL bone arrays");
    if (!append(skb, base)) return Fail(error, "Prepared model exceeds 128 MiB");
    for (uint32_t i = 0; i < bones.count; ++i) {
        const size_t bone = size_t(base) + bones.offset + size_t(i) * 88;
        for (const size_t track : {size_t(16), size_t(36), size_t(56)})
            if (!RebaseTrack(b, bone + track, base, size_t(base) + skb.size)) return Fail(error, "Invalid SKEL bone track");
    }
    SetHeaderArray(b, 0x2c, bones.count, base + bones.offset);
    SetHeaderArray(b, 0x34, keys.count, base + keys.offset);

    if (ska.data) {
        SkelArray attachments, attachmentLookup;
        if (!SkelArrayAt(ska.data, ska.size, 0, 40, attachments) || !SkelArrayAt(ska.data, ska.size, 8, 2, attachmentLookup))
            return Fail(error, "Invalid SKEL attachment arrays");
        if (!append(ska, base)) return Fail(error, "Prepared model exceeds 128 MiB");
        for (uint32_t i = 0; i < attachments.count; ++i)
            if (!RebaseTrack(b, size_t(base) + attachments.offset + size_t(i) * 40 + 20, base, size_t(base) + ska.size))
                return Fail(error, "Invalid SKEL attachment track");
        SetHeaderArray(b, 0xf0, attachments.count, base + attachments.offset);
        SetHeaderArray(b, 0xf8, attachmentLookup.count, base + attachmentLookup.offset);
    }
    return true;
}

bool Validate(const std::vector<uint8_t>& b, const ModelFiles& files, std::string& error) {
    struct Field { size_t offset, stride; };
    const Field fields[] = {
        {8, 1}, {0x14, 4}, {0x1c, 64}, {0x24, 2}, {0x2c, 88}, {0x34, 2}, {0x3c, 48},
        {0x48, 40}, {0x50, 16}, {0x58, 20}, {0x60, 60}, {0x68, 2}, {0x70, 4},
        {0x78, 2}, {0x80, 2}, {0x88, 2}, {0x90, 2}, {0x98, 2}, {0xd8, 2},
        {0xe0, 12}, {0xe8, 12}, {0xf0, 40}, {0xf8, 2}, {0x100, 36}, {0x108, 156},
        {0x110, 100}, {0x118, 2}, {0x120, 176}
    };
    for (const auto& f : fields)
        if (!Array(b, f.offset, f.stride)) return Fail(error, "M2 array exceeds payload size");
    for (uint32_t i = 0; i < U32(b, 0x110); ++i) {
        const size_t p = U32(b, 0x114) + size_t(i) * 100;
        if (!Track(b, p + 16, 36) || !Track(b, p + 48, 36) || !Track(b, p + 80, 12))
            return Fail(error, "Invalid camera track");
    }
    for (uint32_t i = 0; i < U32(b, 0x118); ++i) {
        const auto index = U16(b, U32(b, 0x11c) + size_t(i) * 2);
        if (index != 0xffff && index >= U32(b, 0x110)) return Fail(error, "Invalid camera lookup");
    }
    for (uint32_t i = 0; i < U32(b, 0x120); ++i) {
        const size_t p = U32(b, 0x124) + size_t(i) * 176;
        if (U32(b, p + 4) >= U32(b, 0x2c) || !Array(b, p + 20, 2) || !Array(b, p + 28, 2) ||
            U32(b, p + 20) != 1 || U32(b, p + 28) != 1 || b[p + 175])
            return Fail(error, "Unsupported ribbon bone, texture transform or index array");
        for (auto field : {20u, 28u}) for (uint32_t j = 0; j < U32(b, p + field); ++j)
            if (U16(b, U32(b, p + field + 4) + size_t(j) * 2) >= U32(b, field == 20 ? 0x50 : 0x70))
                return Fail(error, "Ribbon texture/material index out of range");
    }
    if (!String(b, 8)) return Fail(error, "Invalid model name");
    if (!U32(b, 0x2c) || U32(b, 0x2c) > 256) return Fail(error, "Model requires between 1 and 256 embedded bones");
    if (!U32(b, 0x44) || U32(b, 0x44) > 4 || files.skins.size() < U32(b, 0x44))
        return Fail(error, "Missing SFID entries or unsupported skin profile count");
    for (uint32_t i = 0; i < U32(b, 0x44); ++i)
        if (!files.skins[i]) return Fail(error, "Missing skin FileDataID");
    if (!files.textures.empty() && files.textures.size() != U32(b, 0x50))
        return Fail(error, "TXID count differs from texture count");
    for (uint32_t i = 0; i < U32(b, 0x1c); ++i) {
        size_t p = U32(b, 0x20) + size_t(i) * 64;
        uint32_t alias = i;
        for (uint32_t depth = 0; U32(b, U32(b, 0x20) + size_t(alias) * 64 + 12) & 0x40; ++depth) {
            alias = U16(b, U32(b, 0x20) + size_t(alias) * 64 + 62);
            if (alias >= U32(b, 0x1c) || depth >= U32(b, 0x1c)) return Fail(error, "Invalid animation alias chain");
        }
        if (!(U32(b, p + 12) & (0x20 | 0x40))) {
            const auto found = std::find_if(files.animations.begin(), files.animations.end(), [&](const AnimationFile& f) {
                return f.id == U16(b, p) && f.variation == U16(b, p + 2) && f.fileId;
            });
            if (found == files.animations.end()) return Fail(error, "External animation has no AFID entry");
        }
    }
    const Field lookups[] = {{0x24, 0x1c}, {0x34, 0x2c}, {0x78, 0x2c}, {0x80, 0x50}, {0x90, 0x58}, {0x98, 0x60}, {0xf8, 0xf0}};
    for (const auto& f : lookups) {
        for (uint32_t i = 0; i < U32(b, f.offset); ++i) {
            uint16_t v = U16(b, U32(b, f.offset + 4) + size_t(i) * 2);
            if (v != 0xffff && v >= U32(b, f.stride)) return Fail(error, "M2 lookup index is out of range");
        }
    }
    for (uint32_t i = 0; i < U32(b, 0x2c); ++i) {
        size_t p = U32(b, 0x30) + size_t(i) * 88;
        uint16_t parent = U16(b, p + 8);
        if (parent != 0xffff && parent >= i) return Fail(error, "Unsupported bone hierarchy order");
    }
    for (uint32_t i = 0; i < U32(b, 0x3c); ++i) {
        size_t p = U32(b, 0x40) + size_t(i) * 48;
        for (size_t k = 0; k < 4; ++k)
            if (b[p + 12 + k] && b[p + 16 + k] >= U32(b, 0x2c))
                return Fail(error, "Model vertex references an invalid bone");
    }
    for (uint32_t i = 0; i < U32(b, 0x70); ++i)
        if (U16(b, U32(b, 0x74) + size_t(i) * 4 + 2) > 6) return Fail(error, "Unsupported Retail blend mode");
    for (const auto& f : {Field{0xf0, 40}, Field{0x100, 36}, Field{0x108, 156}}) {
        for (uint32_t i = 0; i < U32(b, f.offset); ++i) {
            size_t p = U32(b, f.offset + 4) + size_t(i) * f.stride;
            size_t bone = p + (f.offset == 0x100 ? 8 : f.offset == 0xf0 ? 4 : 2);
            if (U16(b, bone) >= U32(b, 0x2c)) return Fail(error, "Attachment, event or light bone is out of range");
        }
    }
    if (!Tracks(b)) return Fail(error, "Invalid M2 animation track");
    return true;
}
}

std::string FileName(uint32_t id, const char* extension) {
    return "retail/" + std::to_string(id) + extension;
}

bool ReadHelmetAnchor(const std::vector<uint8_t>& b, HelmetAnchor& anchor, std::string& error) {
    anchor = {};
    error.clear();
    if (!Range(b, 0, 0x100) || U32(b, 0) != 0x3032444d || U32(b, 4) != 264)
        return Fail(error, "Helmet donor must be an unrelocated legacy M2");
    if (!Array(b, 0x2c, 88) || !Array(b, 0x34, 2) || !Array(b, 0xf0, 40) || !Array(b, 0xf8, 2)
        || U32(b, 0x34) <= 6 || U32(b, 0xf8) <= 11)
        return Fail(error, "Helmet donor has invalid bone or attachment arrays");
    const uint32_t head = U16(b, U32(b, 0x38) + 6 * 2);
    const uint32_t socket = U16(b, U32(b, 0xfc) + 11 * 2);
    if (head >= U32(b, 0x2c) || socket >= U32(b, 0xf0))
        return Fail(error, "Helmet donor is missing its head or helmet socket");
    const size_t attachment = U32(b, 0xf4) + size_t(socket) * 40;
    if (U32(b, attachment) != 11 || U16(b, attachment + 4) >= U32(b, 0x2c))
        return Fail(error, "Helmet donor socket ID or bone is invalid");
    const size_t pivot = U32(b, 0x30) + size_t(head) * 88 + 76;
    for (size_t axis = 0; axis < 3; ++axis) {
        std::memcpy(&anchor.head[axis], b.data() + pivot + axis * 4, 4);
        std::memcpy(&anchor.socket[axis], b.data() + attachment + 8 + axis * 4, 4);
        if (!std::isfinite(anchor.head[axis]) || !std::isfinite(anchor.socket[axis])) {
            anchor = {};
            return Fail(error, "Helmet donor contains a nonfinite anchor");
        }
    }
    return true;
}

std::array<float, 3> HelmetTranslation(const HelmetAnchor& donor, const HelmetAnchor& target) {
    std::array<float, 3> translation{};
    for (size_t axis = 0; axis < 3; ++axis)
        translation[axis] = target.head[axis] + donor.socket[axis] - donor.head[axis] - target.socket[axis];
    return translation;
}

bool InspectModel(const std::vector<uint8_t>& bytes, ModelFiles& files, std::string& error) {
    error.clear();
    Chunks chunks;
    if (!Parse(bytes, chunks, error)) return false;
    files = std::move(chunks.files);
    return true;
}

bool PrepareModel(const std::vector<uint8_t>& bytes, std::vector<uint8_t>& prepared, ModelFiles& files, std::string& error,
    const std::vector<uint8_t>* skeleton) {
    error.clear();
    prepared.clear();
    Chunks chunks;
    if (!Parse(bytes, chunks, error)) return false;
    files = chunks.files;
    auto& b = chunks.body;
    if (files.version < 272 || files.version > 274) return Fail(error, "Supported Retail M2 versions are 272-274");
    if (files.skeleton) {
        if (!skeleton) return Fail(error, "Model needs its SKEL skeleton file");
        if (!SpliceSkeleton(b, files, *skeleton, error)) return false;
    }
    if (!Array(b, 0x1c, 64)) return Fail(error, "Invalid sequence array");
    // 0x200000 only marks chunked .anim files (AFM2 wraps the legacy payload); see StripAnimationChunk.
    // Retail particle emitters use a newer layout the legacy emitter cannot read. Omit them; the mesh,
    // skinning, ribbons and animation are unaffected. Documented limitation in retail-native-mounts.md.
    Put(b, 0x128, 0);
    if (!Cameras(b, error)) return false;
    if ((U32(b, 0x10) & 8) && !Array(b, 0x130, 2)) return Fail(error, "Invalid texture combiner array");
    // Retail added blend modes 7-14 beyond the legacy 7-entry table. Map each onto the closest legacy mode
    // and drop render flags the legacy renderer does not define.
    if (Array(b, 0x70, 4)) {
        static const uint8_t kBlend[] = {0, 1, 2, 3, 4, 5, 6, 4, 4, 2, 2, 3, 2, 4, 4};
        for (uint32_t i = 0; i < U32(b, 0x70); ++i) {
            const size_t m = U32(b, 0x74) + size_t(i) * 4;
            const uint16_t blend = U16(b, m + 2);
            if (blend < sizeof(kBlend)) { b[m + 2] = kBlend[blend]; b[m + 3] = 0; }
            b[m] &= 0x1f; b[m + 1] = 0;
        }
    }
    if (!Validate(b, files, error)) return false;
    if (U32(b, 0x50) > 256 || U32(b, 0x60) > 255) return Fail(error, "Retail texture/transform count exceeds legacy renderer limits");
    // Retail split the old 32-bit blend duration into two 16-bit durations.
    // The legacy animation mixer has one duration, so use the incoming blend time.
    for (uint32_t i = 0; i < U32(b, 0x1c); ++i) {
        const size_t p = U32(b, 0x20) + size_t(i) * 64;
        if (U16(b, p + 16) > 0x7fff) return Fail(error, "Negative Retail animation frequency is unsupported");
        Put(b, p + 16, U16(b, p + 16));
        Put(b, p + 28, U16(b, p + 28));
    }
    // 3.3.5 plays SwimIdle (41) / Swim (42) for any flying unit; Retail flying models author flight
    // under FlyStand (229) / FlyFly (364) and keep real swim loops under 41/42. Swap them so the
    // legacy lookup finds flight. Swap the ids and the first-index hash entries together.
    {
        const size_t seq = U32(b, 0x20), hash = U32(b, 0x28);
        const uint32_t count = U32(b, 0x1c), hashCount = U32(b, 0x24);
        auto first = [&](uint16_t id) {
            for (uint32_t i = 0; i < count; ++i) if (U16(b, seq + size_t(i) * 64) == id) return int32_t(i);
            return int32_t(-1);
        };
        auto put16 = [&](size_t at, uint16_t v) { b[at] = uint8_t(v); b[at + 1] = uint8_t(v >> 8); };
        auto swapIds = [&](uint16_t x, uint16_t y) {
            const int32_t ix = first(x), iy = first(y);
            if (ix < 0 || iy < 0) return;
            for (uint32_t i = 0; i < count; ++i) {
                const size_t at = seq + size_t(i) * 64;
                if (U16(b, at) == x) put16(at, y); else if (U16(b, at) == y) put16(at, x);
            }
            for (uint32_t i = 0; i < hashCount; ++i) {
                const uint16_t v = U16(b, hash + size_t(i) * 2);
                if (v == ix) put16(hash + size_t(i) * 2, uint16_t(iy)); else if (v == iy) put16(hash + size_t(i) * 2, uint16_t(ix));
            }
        };
        if (first(135) >= 0 && first(229) >= 0 && first(364) >= 0 && first(41) >= 0 && first(42) >= 0) {
            swapIds(41, 229);
            swapIds(42, 364);
        }
    }
    // TXID replaces embedded filenames in modern models. Append stable virtual names;
    // every other array retains its original payload-relative offset.
    for (uint32_t i = 0; i < U32(b, 0x50); ++i) {
        const size_t p = U32(b, 0x54) + size_t(i) * 16;
        if (U32(b, p)) continue; // Replaceable textures are supplied by display info.
        std::string name;
        if (!files.textures.empty() && files.textures[i]) {
            name = FileName(files.textures[i], ".blp");
        } else {
            if (!String(b, p + 8) || !U32(b, p + 8)) return Fail(error, "Texture has neither TXID nor filename");
            name = "retail/" + std::string(reinterpret_cast<const char*>(b.data() + U32(b, p + 12)));
        }
        if (b.size() + name.size() + 1 > 128 * 1024 * 1024) return Fail(error, "Prepared model exceeds 128 MiB");
        Put(b, p + 8, uint32_t(name.size() + 1));
        Put(b, p + 12, uint32_t(b.size()));
        b.insert(b.end(), name.begin(), name.end());
        b.push_back(0);
    }
    // Retail's 0xFFFF opacity lookup denotes constant one. The legacy renderer
    // indexes every referenced opacity channel directly, so give it a real default track.
    bool defaultWeight = !U32(b, 0x90);
    for (uint32_t i = 0; i < U32(b, 0x90); ++i)
        defaultWeight |= U16(b, U32(b, 0x94) + size_t(i) * 2) == 0xffff;
    if (defaultWeight) {
        const uint32_t count = U32(b, 0x58), old = U32(b, 0x5c);
        if (count >= 0xffff) return Fail(error, "Too many opacity tracks");
        std::vector<uint8_t> weights(b.begin() + old, b.begin() + old + size_t(count) * 20);
        weights.resize(weights.size() + 20);
        weights[size_t(count) * 20 + 2] = weights[size_t(count) * 20 + 3] = 0xff;
        while (b.size() % 4) b.push_back(0);
        Put(b, 0x58, count + 1); Put(b, 0x5c, uint32_t(b.size()));
        b.insert(b.end(), weights.begin(), weights.end());
        if (!U32(b, 0x90)) {
            Put(b, 0x90, 1); Put(b, 0x94, uint32_t(b.size()));
            b.push_back(uint8_t(count)); b.push_back(uint8_t(count >> 8));
        } else {
            for (uint32_t i = 0; i < U32(b, 0x90); ++i) {
                size_t p = U32(b, 0x94) + size_t(i) * 2;
                if (U16(b, p) == 0xffff) { b[p] = uint8_t(count); b[p + 1] = uint8_t(count >> 8); }
            }
        }
    }
    // These versions share the validated fields above with the engine's M2Data. This
    // change is made only after unsupported layouts and dependencies have been rejected.
    Put(b, 4, 264);
    if (b.size() < 312) b.resize(312); // M2Data includes the optional combiner descriptor.
    prepared = std::move(b);
    return true;
}

bool ValidateSkin(const std::vector<uint8_t>& b, const std::vector<uint8_t>& m, std::string& error) {
    if (!Range(b, 0, 48) || U32(b, 0) != 0x4e494b53 || !Range(m, 0, 0x130)) return Fail(error, "Invalid SKIN header");
    for (const auto& f : {std::pair<size_t, size_t>{4, 2}, {12, 2}, {20, 4}, {28, 48}, {36, 24}})
        if (!Array(b, f.first, f.second)) return Fail(error, "SKIN array exceeds file size");
    // Retail may leave boneCountMax at zero; the engine then uses one instance.
    if (U32(b, 44) > 256 || U32(b, 4) != U32(b, 20)) return Fail(error, "Invalid SKIN bone palette");
    for (uint32_t i = 0; i < U32(b, 4); ++i)
        if (U16(b, U32(b, 8) + size_t(i) * 2) >= U32(m, 0x3c)) return Fail(error, "SKIN vertex is out of range");
    for (uint32_t i = 0; i < U32(b, 12); ++i)
        if (U16(b, U32(b, 16) + size_t(i) * 2) >= U32(b, 4)) return Fail(error, "SKIN triangle is out of range");
    for (uint32_t i = 0; i < U32(b, 28); ++i) {
        size_t p = U32(b, 32) + size_t(i) * 48;
        uint32_t first = (uint32_t(U16(b, p + 2)) << 16) | U16(b, p + 8);
        uint32_t bones = U16(b, p + 12), combo = U16(b, p + 14), influences = U16(b, p + 16);
        if (!Index(b, 4, U16(b, p + 4), U16(b, p + 6)) || !Index(b, 12, first, U16(b, p + 10)) ||
            !bones || bones > 256 || (U32(b, 44) && bones > U32(b, 44)) || influences > 4 || !Index(m, 0x78, combo, bones))
            return Fail(error, "Invalid SKIN section or bone palette range");
        for (uint32_t v = U16(b, p + 4); v < uint32_t(U16(b, p + 4)) + U16(b, p + 6); ++v) {
            size_t vertex = U32(m, 0x40) + size_t(U16(b, U32(b, 8) + size_t(v) * 2)) * 48;
            for (uint32_t k = 0; k < 4; ++k) {
                if (m[vertex + 12 + k] && (k >= influences || b[U32(b, 24) + size_t(v) * 4 + k] >= bones))
                    return Fail(error, "SKIN vertex bone index is out of range");
            }
        }
    }
    for (uint32_t i = 0; i < U32(b, 36); ++i) {
        size_t p = U32(b, 40) + size_t(i) * 24;
        uint32_t n = U16(b, p + 14);
        uint16_t shader = U16(b, p + 2);
        if (!n || n > 3 || (n == 3 && shader != 0x8010) ||
            ((shader & 0x8000) && !((shader >= 0x8001 && shader <= 0x8003 && n == 2) ||
                (shader == 0x8010 && n == 3) || ((shader == 0x8011 || shader == 0x8012) && n == 2))))
            return Fail(error, "Retail shader or texture count is not supported yet");
        // The batch geoset index (+6) is unused by the legacy renderer and may exceed the section count in newer models.
        const bool section = U16(b, p + 4) < U32(b, 28);
        const bool material = U16(b, p + 10) < U32(m, 0x70);
        const bool color = U16(b, p + 8) == 0xffff || U16(b, p + 8) < U32(m, 0x48);
        const bool textures = Index(m, 0x80, U16(b, p + 16), n);
        const bool coords = !U32(m, 0x88) || Index(m, 0x88, U16(b, p + 18), n);
        const bool weights = Index(m, 0x90, U16(b, p + 20), 1);
        const bool transforms = Index(m, 0x98, U16(b, p + 22), n);
        if (!(section && material && color && textures && coords && weights && transforms)) {
            char text[160];
            std::snprintf(text, sizeof(text), "SKIN batch %u references an invalid model table (section %d material %d color %d textures %d coords %d weights %d transforms %d)",
                i, section, material, color, textures, coords, weights, transforms);
            return Fail(error, text);
        }
    }
    return true;
}

bool InspectAnimation(const uint8_t* bytes, size_t size, AnimationLayout& layout, std::string& error) {
    layout = {};
    error.clear();
    auto tagAt = [&](size_t at) {
        uint32_t tag;
        std::memcpy(&tag, bytes + at, 4);
        return tag;
    };
    const uint32_t first = size >= 4 ? tagAt(0) : 0;
    if (first != 0x324d4641 && first != 0x42534641 && first != 0x41534641) {
        layout.model = layout.bones = layout.attachments = {0, size, true};
        return true;
    }
    layout.chunked = true;
    for (size_t p = 0; p < size;) {
        if (size - p < 8) return Fail(error, "Truncated animation chunk header");
        const uint32_t tag = tagAt(p), count = tagAt(p + 4);
        p += 8;
        if (count > size - p) return Fail(error, "Animation chunk exceeds file size");
        AnimationSpan* span = tag == 0x324d4641 ? &layout.model
            : tag == 0x42534641 ? &layout.bones : tag == 0x41534641 ? &layout.attachments : nullptr;
        if (span) {
            if (span->present) return Fail(error, "Duplicate animation payload chunk");
            *span = {p, count, true};
        }
        p += count;
    }
    return true;
}

bool StripAnimationChunk(std::vector<uint8_t>& bytes, std::string& error) {
    AnimationLayout layout;
    if (!InspectAnimation(bytes.data(), bytes.size(), layout, error)) return false;
    if (!layout.chunked || layout.bones.present || layout.attachments.present) return true;
    const auto model = layout.model;
    bytes = std::vector<uint8_t>(bytes.begin() + model.offset, bytes.begin() + model.offset + model.size);
    return true;
}

bool ValidateAnimation(const std::vector<uint8_t>& b, const std::vector<uint8_t>& m,
    uint16_t id, uint16_t variation, std::string& error) {
    if (!Range(m, 0, 0x130) || !Array(m, 0x1c, 64)) return Fail(error, "Invalid animation owner");
    AnimationLayout layout;
    if (!InspectAnimation(b.data(), b.size(), layout, error)) return false;
    auto payload = [&](const AnimationSpan& span) {
        return std::vector<uint8_t>(b.begin() + span.offset, b.begin() + span.offset + span.size);
    };
    // The engine relocates the same spans below by track category. In particular, a short AFM2
    // material payload is not the address space of the much larger AFSB skeletal keyframes.
    std::vector<uint8_t> model, bones, attachments;
    if (layout.chunked) {
        model = payload(layout.model);
        bones = payload(layout.bones.present ? layout.bones : layout.model);
        attachments = payload(layout.attachments.present ? layout.attachments : layout.model);
    }
    for (uint32_t i = 0; i < U32(m, 0x1c); ++i) {
        size_t p = U32(m, 0x20) + size_t(i) * 64;
        if (U16(m, p) == id && U16(m, p + 2) == variation) {
            if (!Tracks(m, layout.chunked ? &model : &b, i,
                layout.chunked ? &bones : nullptr, layout.chunked ? &attachments : nullptr))
                return Fail(error, ("External animation track exceeds file size: " + s_trackFailure).c_str());
            return true;
        }
    }
    return Fail(error, "Animation is not referenced by model");
}

std::string HexWord(uint16_t v) {
    char text[8];
    std::snprintf(text, sizeof(text), "%04x", v);
    return text;
}

bool PrepareSkin(std::vector<uint8_t>& b, const std::vector<uint8_t>& m, std::string& error, bool degradeShaders) {
    if (!Range(b, 0, 48) || !Range(m, 0, 0x130) || !Array(b, 4, 2) || !Array(b, 28, 48) || !Array(m, 0x3c, 48))
        return Fail(error, "Invalid SKIN arrays");
    // Retail leaves the old per-section influence count unused (zero). The legacy
    // renderer uses it to choose a vertex shader and remap the bone palette.
    for (uint32_t i = 0; i < U32(b, 28); ++i) {
        size_t p = U32(b, 32) + size_t(i) * 48;
        if (U16(b, p + 16)) continue;
        uint16_t influences = 0;
        if (!Index(b, 4, U16(b, p + 4), U16(b, p + 6))) return Fail(error, "Invalid SKIN vertex range");
        for (uint32_t j = U16(b, p + 4); j < uint32_t(U16(b, p + 4)) + U16(b, p + 6); ++j) {
            uint16_t index = U16(b, U32(b, 8) + size_t(j) * 2);
            if (index >= U32(m, 0x3c)) return Fail(error, "Invalid SKIN vertex index");
            size_t vertex = U32(m, 0x40) + size_t(index) * 48;
            for (uint16_t k = 0; k < 4; ++k) if (m[vertex + 12 + k]) influences = std::max(influences, uint16_t(k + 1));
        }
        b[p + 16] = uint8_t(influences);
        b[p + 17] = 0;
    }
    if (!Array(b, 36, 24)) return Fail(error, "Invalid SKIN batches");
    // Retail explicit shader indices 0-2 name the same three T1/Env combiners
    // as legacy indices 1-3. Legacy index 0 is a disabled-batch marker.
    // Reference: wowmodelviewer Tools/UnityRendererProject/Assets/Scripts/Wow/M2ShaderTable.cs.
    for (uint32_t i = 0; i < U32(b, 36); ++i) {
        size_t p = U32(b, 40) + size_t(i) * 24 + 2;
        uint16_t shader = U16(b, p);
        if (shader >= 0x8000 && shader <= 0x8002) {
            ++shader;
            b[p] = uint8_t(shader);
            b[p + 1] = uint8_t(shader >> 8);
        } else if (shader == 0x8003 || shader == 0x4014) {
            shader = shader == 0x8003 ? 0x8010 : 0x8011;
            b[p] = uint8_t(shader); b[p + 1] = uint8_t(shader >> 8);
        } else if (shader == 0x8015) {
            // Diffuse_EdgeFade_T1_T2 / Combiners_Mod_Mod: the 3.3.5 client has no edge-fade vertex shader, so draw it without the fade.
            b[p] = 0x12; b[p + 1] = 0x80;
        } else if ((shader & 0x8000) && degradeShaders) {
            // Character layers (tabards, armor effects) use multi-texture shaders the legacy renderer lacks.
            // Draw them with their first texture and the plain opaque combiner instead of failing the model.
            b[p] = b[p + 1] = 0;
            b[p + 12] = 1; b[p + 13] = 0;
        } else if (shader & 0x8000) {
            return Fail(error, ("Retail explicit shader is not implemented yet: 0x" + HexWord(shader) + " in batch " + std::to_string(i) + " (textures " + std::to_string(U16(b, U32(b, 40) + size_t(i) * 24 + 14)) + ")").c_str());
        }
    }
    // Diagnostic: WOW_RETAIL_HIDE_BATCHES=1,5 drops those batch indices (of the original skin) from every Retail model.
    if (const char* hide = std::getenv("WOW_RETAIL_HIDE_BATCHES")) {
        std::vector<uint32_t> drop;
        for (const char* c = hide; *c;) {
            drop.push_back(uint32_t(std::strtoul(c, const_cast<char**>(&c), 10)));
            if (*c == ',') ++c; else if (*c) break;
        }
        const size_t base = U32(b, 40);
        uint32_t kept = 0;
        for (uint32_t i = 0; i < U32(b, 36); ++i) {
            if (std::find(drop.begin(), drop.end(), i) != drop.end()) continue;
            if (kept != i) std::memmove(b.data() + base + size_t(kept) * 24, b.data() + base + size_t(i) * 24, 24);
            ++kept;
        }
        Put(b, 36, kept);
    }
    return ValidateSkin(b, m, error);
}
}
