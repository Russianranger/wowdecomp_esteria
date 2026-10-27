#ifndef ENGINE_SOURCE_RETAIL_ETHEREALMODEL_H
#define ENGINE_SOURCE_RETAIL_ETHEREALMODEL_H

#include "Engine/Source/Retail/Ethereal.h"
#include "Engine/Source/Retail/RetailModel.h"
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>

namespace Retail::Ethereal::Motion {
using namespace SlimeGiant;
using Bytes = std::vector<uint8_t>;
using Reader = std::function<bool(uint32_t, Bytes&, std::string&)>;
inline bool Range(const Bytes& b, size_t offset, size_t size) {
    return offset <= b.size() && size <= b.size() - offset;
}
inline bool Fail(std::string& error, const char* reason) { error = reason; return false; }
inline void HalfPut(Bytes& b, size_t at, uint16_t value) { std::memcpy(b.data() + at, &value, 2); }
inline size_t Append(Bytes& out, const Bytes& bytes) {
    while (out.size() % 4) out.push_back(0);
    const size_t at = out.size();
    if (bytes.size() > 128 * 1024 * 1024 - std::min(size_t(128 * 1024 * 1024), at))
        throw std::runtime_error("Ethereal motion exceeds the 128 MiB model limit");
    out.insert(out.end(), bytes.begin(), bytes.end());
    return at;
}
struct TrackRef { size_t at, key; uint32_t category; bool event = false, spline = false; };
inline bool Tracks(const Bytes& b, bool raw, bool bonesOnly, std::vector<TrackRef>& tracks) {
    struct Group { size_t array, stride, at, key; uint32_t category; };
    const Group groups[] = {
        {0x2c,88,16,12,1}, {0x2c,88,36,8,1}, {0x2c,88,56,12,1},
        {0x48,40,0,12,0}, {0x48,40,20,2,0}, {0x58,20,0,2,0},
        {0x60,60,0,12,0}, {0x60,60,20,16,0}, {0x60,60,40,12,0},
        {0xf0,40,20,1,2}, {0x100,36,24,0,0},
        {0x108,156,16,12,0}, {0x108,156,36,4,0}, {0x108,156,56,12,0},
        {0x108,156,76,4,0}, {0x108,156,96,4,0}, {0x108,156,116,4,0}, {0x108,156,136,1,0},
        {0x120,176,36,12,0}, {0x120,176,56,2,0}, {0x120,176,76,4,0},
        {0x120,176,96,4,0}, {0x120,176,132,2,0}, {0x120,176,152,1,0}
    };
    for (const auto& group : groups) {
        if (bonesOnly && group.category != 1) continue;
        if (!Array(b, group.array, group.stride)) return false;
        for (uint32_t i = 0; i < Word(b, group.array); ++i)
            tracks.push_back({Word(b, group.array + 4) + size_t(i) * group.stride + group.at,
                group.key, group.category, !group.key});
    }
    if (bonesOnly) return true;
    if (!Array(b, 0x128, raw ? 492 : 476) || !Array(b, 0x110, raw ? 116 : 100)) return false;
    for (uint32_t i = 0; i < Word(b, 0x128); ++i) {
        const size_t at = Word(b, 0x12c) + size_t(i) * (raw ? 492 : 476);
        for (size_t offset : {0x34u,0x48u,0x5cu,0x70u,0x84u,0x98u,0xb0u,0xc8u,0xdcu,0xf0u})
            tracks.push_back({at + offset, 4, 0});
        tracks.push_back({at + 0x1c8, 1, 0});
    }
    for (uint32_t i = 0; i < Word(b, 0x110); ++i) {
        const size_t at = Word(b, 0x114) + size_t(i) * (raw ? 116 : 100);
        for (size_t offset : {size_t(raw ? 12 : 16), size_t(raw ? 44 : 48), size_t(raw ? 76 : 80)})
            tracks.push_back({at + offset, offset == size_t(raw ? 76 : 80) ? 12u : 36u, 0, false, true});
        if (raw) tracks.push_back({at + 96, 12, 0, false, true});
    }
    return true;
}
struct Keys { Bytes times, values; uint16_t interpolation = 0; };
inline bool KeysAt(const Bytes& b, size_t track, size_t keySize, uint32_t index, Keys& keys, bool spline = false) {
    if (!Range(b, track, keySize ? 20 : 12) || !Array(b, track + 4, 8) ||
        (keySize && (!Array(b, track + 12, 8) || Word(b, track + 4) != Word(b, track + 12)))) return false;
    keys = {}; keys.interpolation = Half(b, track);
    if (keys.interpolation > 3) return false;
    if (Half(b, track + 2) != 65535) index = 0;
    if (index >= Word(b, track + 4)) return true;
    const size_t time = Word(b, track + 8) + size_t(index) * 8;
    const size_t value = keySize ? Word(b, track + 16) + size_t(index) * 8 : 0;
    const uint32_t count = Word(b, time);
    const size_t stride = keySize * (!spline && keys.interpolation >= 2 ? 3 : 1);
    if (!Range(b, Word(b, time + 4), size_t(count) * 4) ||
        (keySize && (count != Word(b, value) || !Range(b, Word(b, value + 4), size_t(count) * stride)))) return false;
    keys.times.assign(b.begin() + Word(b, time + 4), b.begin() + Word(b, time + 4) + size_t(count) * 4);
    if (keySize) keys.values.assign(b.begin() + Word(b, value + 4), b.begin() + Word(b, value + 4) + size_t(count) * stride);
    return true;
}
inline void StoreTrack(Bytes& b, size_t at, size_t keySize, const std::vector<Keys>& keys) {
    Bytes times(keys.size() * 8), values(keySize ? keys.size() * 8 : 0);
    for (size_t i = 0; i < keys.size(); ++i) {
        Put(times, i * 8, uint32_t(keys[i].times.size() / 4));
        if (!keys[i].times.empty()) Put(times, i * 8 + 4, uint32_t(Append(b, keys[i].times)));
        if (keySize) {
            Put(values, i * 8, uint32_t(keys[i].times.size() / 4));
            if (!keys[i].values.empty()) Put(values, i * 8 + 4, uint32_t(Append(b, keys[i].values)));
        }
    }
    Put(b, at + 4, uint32_t(keys.size())); Put(b, at + 8, uint32_t(Append(b, times)));
    if (keySize) { Put(b, at + 12, uint32_t(keys.size())); Put(b, at + 16, uint32_t(Append(b, values))); }
}

// Resolve each file's own AFM2/AFSB/AFSA address space before making any sequence resident.
inline bool Embed(Bytes& b, const ModelFiles& graph, const Reader& read, bool raw, bool bonesOnly, std::string& error) {
    const Bytes original = b;
    if (!Array(original, 0x1c, 64)) return Fail(error, "Invalid Ethereal sequence array");
    const uint32_t count = Word(original, 0x1c);
    std::vector<TrackRef> tracks;
    if (!Tracks(original, raw, bonesOnly, tracks)) return Fail(error, "Invalid Ethereal track arrays");
    std::map<uint32_t, Bytes> files;
    for (const auto& track : tracks) {
        if (!Range(original, track.at, track.key ? 20 : 12)) return Fail(error, "Invalid Ethereal track header");
        // One span is shared by every animation; expanding it creates empty color/effect tracks outside idle.
        const uint32_t spans = Word(original, track.at + 4);
        const uint32_t slots = Half(original, track.at + 2) == 65535 && spans > 1 ? count : spans;
        std::vector<Keys> keys(slots);
        for (uint32_t i = 0; i < slots; ++i) {
            uint32_t owner = i;
            if (Half(original, track.at + 2) == 65535) {
                for (uint32_t depth = 0; Word(original, Word(original, 0x20) + size_t(owner) * 64 + 12) & 0x40; ++depth) {
                    owner = Half(original, Word(original, 0x20) + size_t(owner) * 64 + 62);
                    if (owner >= count || depth >= count) return Fail(error, "Invalid Ethereal alias owner");
                }
            }
            const size_t sequence = Word(original, 0x20) + size_t(owner) * 64;
            const bool external = Half(original, track.at + 2) == 65535 && !(Word(original, sequence + 12) & 0x20);
            if (!external) {
                if (!KeysAt(original, track.at, track.key, owner, keys[i], track.spline)) return Fail(error, "Invalid resident Ethereal keys");
                continue;
            }
            if (!Array(original, track.at + 4, 8) || (track.key && !Array(original, track.at + 12, 8)))
                return Fail(error, "Invalid external Ethereal metadata");
            if (track.key && Word(original, track.at + 4) != Word(original, track.at + 12))
                return Fail(error, "External Ethereal timestamp/value array counts differ");
            if (owner >= Word(original, track.at + 4)) continue;
            const size_t time = Word(original, track.at + 8) + size_t(owner) * 8;
            const uint32_t n = Word(original, time);
            if (!n) continue;
            const auto file = std::find_if(graph.animations.begin(), graph.animations.end(), [&](const AnimationFile& f) {
                return f.id == Half(original, sequence) && f.variation == Half(original, sequence + 2);
            });
            if (file == graph.animations.end() || !file->fileId) return Fail(error, "Ethereal motion has no AFID owner");
            auto& bytes = files[file->fileId];
            if (bytes.empty() && !read(file->fileId, bytes, error)) return false;
            AnimationLayout layout;
            if (!InspectAnimation(bytes.data(), bytes.size(), layout, error)) return false;
            const auto span = track.category == 1 && layout.bones.present ? layout.bones
                : track.category == 2 && layout.attachments.present ? layout.attachments : layout.model;
            if (!span.present) return Fail(error, "Ethereal animation lacks its required payload category");
            const size_t value = track.key ? Word(original, track.at + 16) + size_t(owner) * 8 : 0;
            const size_t stride = track.key * (!track.spline && Half(original, track.at) >= 2 ? 3 : 1);
            const size_t to = Word(original, time + 4), vo = track.key ? Word(original, value + 4) : 0;
            if (to > span.size || size_t(n) * 4 > span.size - to ||
                (track.key && (n != Word(original, value) || vo > span.size || size_t(n) * stride > span.size - vo)))
                return Fail(error, "External Ethereal keys exceed their owning payload");
            keys[i].interpolation = Half(original, track.at);
            keys[i].times.assign(bytes.begin() + span.offset + to, bytes.begin() + span.offset + to + size_t(n) * 4);
            if (track.key) keys[i].values.assign(bytes.begin() + span.offset + vo, bytes.begin() + span.offset + vo + size_t(n) * stride);
        }
        StoreTrack(b, track.at, track.key, keys);
    }
    for (uint32_t i = 0; i < count; ++i) {
        const size_t at = Word(b, 0x20) + size_t(i) * 64;
        Put(b, at + 12, (Word(b, at + 12) & ~0x200040u) | 0x20);
        HalfPut(b, at + 62, uint16_t(i));
    }
    return true;
}

using Vector = std::array<float, 3>;
inline Vector Vec(const Bytes& b, size_t at) { Vector value; std::memcpy(value.data(), b.data() + at, 12); return value; }
inline float Distance(const Vector& a, const Vector& b) {
    float sum = 0; for (size_t i = 0; i < 3; ++i) sum += (a[i] - b[i]) * (a[i] - b[i]);
    return std::sqrt(sum);
}

inline bool Complete(Bytes& b, const Bytes& donor, std::string& error) {
    if (!Array(b, 0x2c, 88) || !Array(donor, 0x2c, 88) || !Array(b, 0x1c, 64) || !Array(donor, 0x1c, 64))
        return Fail(error, "Invalid player animation source arrays");
    const Bytes original = b;
    std::map<uint32_t, uint32_t> names, mapping, inverse;
    for (uint32_t i = 0; i < Word(donor, 0x2c); ++i) {
        const uint32_t crc = Word(donor, Word(donor, 0x30) + size_t(i) * 88 + 12);
        if (crc && !names.emplace(crc, i).second) return Fail(error, "Ambiguous Nightborne bone identity");
    }
    for (uint32_t i = 0; i < Word(b, 0x2c); ++i) {
        const size_t at = Word(b, 0x30) + size_t(i) * 88;
        const auto found = names.find(Word(b, at + 12));
        if (found != names.end()) { mapping[i] = found->second; inverse[found->second] = i; }
        else if (static_cast<int32_t>(Word(b, at)) >= 0) return Fail(error, "Nightborne donor lacks an Ethereal key bone");
    }
    const auto parent = [](const Bytes& model, uint32_t bone) {
        return static_cast<int16_t>(Half(model, Word(model, 0x30) + size_t(bone) * 88 + 8));
    };
    const auto keyBone = [](const Bytes& model, uint32_t key) {
        for (uint32_t i = 0; i < Word(model, 0x2c); ++i)
            if (Word(model, Word(model, 0x30) + size_t(i) * 88) == key) return int32_t(i);
        return int32_t(-1);
    };
    // Ethereal leg joints were renamed; both rigs retain the foot keys and the same thigh/knee ancestry.
    for (uint32_t key : {59u,60u}) {
        const int32_t foot = keyBone(original, key), donorFoot = keyBone(donor, key);
        if (foot < 0 || donorFoot < 0) return Fail(error, "Missing Ethereal/Nightborne foot anchor");
        int32_t target = parent(original, uint32_t(foot));
        int32_t provider = parent(donor, uint32_t(donorFoot));
        if (target < 0 || provider < 0) return Fail(error, "Missing Ethereal/Nightborne knee chain");
        provider = parent(donor, uint32_t(provider)); // The donor has an additional ankle helper above its foot key.
        if (provider < 0) return Fail(error, "Missing Nightborne knee above the ankle helper");
        const uint32_t knee = uint32_t(target), donorKnee = uint32_t(provider);
        for (uint32_t auxiliary = 0; auxiliary < Word(original, 0x2c); ++auxiliary) {
            if (auxiliary == knee || parent(original, auxiliary) != parent(original, knee) ||
                Distance(Vec(original, Word(original, 0x30) + size_t(auxiliary) * 88 + 76),
                    Vec(original, Word(original, 0x30) + size_t(knee) * 88 + 76)) > .0001f) continue;
            for (uint32_t counterpart = 0; counterpart < Word(donor, 0x2c); ++counterpart) {
                if (counterpart == donorKnee || parent(donor, counterpart) != parent(donor, donorKnee) ||
                    Distance(Vec(donor, Word(donor, 0x30) + size_t(counterpart) * 88 + 76),
                        Vec(donor, Word(donor, 0x30) + size_t(donorKnee) * 88 + 76)) > .0001f) continue;
                mapping[auxiliary] = counterpart; inverse[counterpart] = auxiliary;
                break;
            }
        }
        for (uint32_t level = 0; level < 3; ++level) {
            if (level == 2) provider = keyBone(donor, 51); // Resolve the pelvis independently of extra hip helpers.
            if (target < 0 || provider < 0) return Fail(error, "Truncated Ethereal/Nightborne leg ancestry");
            mapping[uint32_t(target)] = uint32_t(provider);
            inverse[uint32_t(provider)] = uint32_t(target);
            target = parent(original, uint32_t(target));
            provider = parent(donor, uint32_t(provider));
        }
    }
    std::set<uint32_t> incompatible;
    for (const auto& pair : mapping) {
        int32_t a = parent(b, pair.first), d = parent(donor, pair.second);
        for (uint32_t depth = 0; a >= 0 && !mapping.count(uint32_t(a)); ++depth) {
            if (uint32_t(a) >= Word(b, 0x2c) || depth >= Word(b, 0x2c)) return Fail(error, "Invalid Ethereal bone ancestry");
            a = parent(b, uint32_t(a));
        }
        for (uint32_t depth = 0; d >= 0 && !inverse.count(uint32_t(d)); ++depth) {
            if (uint32_t(d) >= Word(donor, 0x2c) || depth >= Word(donor, 0x2c)) return Fail(error, "Invalid Nightborne bone ancestry");
            d = parent(donor, uint32_t(d));
        }
        if ((a < 0 ? -1 : int32_t(mapping.at(uint32_t(a)))) != d) incompatible.insert(pair.first);
    }
    for (uint32_t bone : incompatible) {
        const auto key = static_cast<int32_t>(Word(b, Word(b, 0x30) + size_t(bone) * 88));
        if ((key >= 0 && key <= 18) || key == 54)
            return Fail(error, "Nightborne key-bone parent axes are incompatible");
        // Name/accessory anchors retain their source motion when their parent chains differ.
        inverse.erase(mapping.at(bone)); mapping.erase(bone);
    }
    float rigScale = 1;
    for (const auto& pair : mapping) {
        const size_t at = Word(original, 0x30) + size_t(pair.first) * 88;
        if (Word(original, at) != 26 || !mapping.count(0)) continue;
        const float targetHeight = Distance(Vec(original, at + 76), Vec(original, Word(original, 0x30) + 76));
        const float donorHeight = Distance(Vec(donor, Word(donor, 0x30) + size_t(pair.second) * 88 + 76),
            Vec(donor, Word(donor, 0x30) + size_t(mapping.at(0)) * 88 + 76));
        if (targetHeight > .01f && donorHeight > .01f) rigScale = targetHeight / donorHeight;
    }
    if (!std::isfinite(rigScale) || rigScale < .1f || rigScale > 10)
        return Fail(error, "Incompatible Nightborne root-to-pelvis proportions");
    const uint32_t oldCount = Word(b, 0x1c);
    std::set<uint16_t> actions;
    for (uint32_t i = 0; i < oldCount; ++i) actions.insert(Half(b, Word(b, 0x20) + size_t(i) * 64));
    std::vector<uint32_t> borrowed, destinations;
    Bytes sequences(b.begin() + Word(b, 0x20), b.begin() + Word(b, 0x20) + size_t(oldCount) * 64);
    for (uint32_t i = 0; i < Word(donor, 0x1c); ++i) {
        const size_t at = Word(donor, 0x20) + size_t(i) * 64;
        const uint16_t id = Half(donor, at);
        const bool replace = id == 84 || id == 209; // Point/unsheathed Point use the player rig rather than the NPC clip.
        if (id > 225 || (actions.count(id) && !replace)) continue;
        uint32_t target = uint32_t(sequences.size() / 64);
        if (replace) {
            for (uint32_t j = 0; j < oldCount; ++j)
                if (Half(sequences, size_t(j) * 64) == id &&
                    Half(sequences, size_t(j) * 64 + 2) == Half(donor, at + 2)) { target = j; break; }
        }
        borrowed.push_back(i);
        destinations.push_back(target);
        if (target == sequences.size() / 64) sequences.insert(sequences.end(), donor.begin() + at, donor.begin() + at + 64);
        else std::copy_n(donor.begin() + at, 64, sequences.begin() + size_t(target) * 64);
    }
    const uint32_t total = uint32_t(sequences.size() / 64);
    if (total >= 32767) return Fail(error, "Ethereal animation count exceeds signed variation capacity");
    std::vector<TrackRef> tracks;
    if (!Tracks(original, true, false, tracks)) return Fail(error, "Invalid Ethereal animation track layout");
    for (const auto& track : tracks) {
        if (Half(original, track.at + 2) != 65535) continue;
        const uint32_t spans = Word(original, track.at + 4);
        if (track.category != 1 && spans <= 1) continue;
        std::vector<Keys> keys(total);
        for (uint32_t i = 0; i < oldCount; ++i)
            if (!KeysAt(original, track.at, track.key, spans == 1 ? 0 : i, keys[i], track.spline))
                return Fail(error, "Invalid original Ethereal motion");
        const bool boneTrack = track.category == 1;
        const uint32_t bone = boneTrack ? uint32_t((track.at - Word(original, 0x30)) / 88) : 0;
        const size_t offset = boneTrack ? (track.at - Word(original, 0x30)) % 88 : 0;
        const auto match = boneTrack ? mapping.find(bone) : mapping.end();
        const size_t donorTrack = match == mapping.end() ? 0 : Word(donor, 0x30) + size_t(match->second) * 88 + offset;
        Keys rest;
        if (!KeysAt(original, track.at, track.key, 0, rest, track.spline)) return Fail(error, "Invalid Ethereal reference pose");
        float scale = rigScale;
        if (donorTrack && offset == 16) {
            const int32_t a = parent(original, bone), d = parent(donor, match->second);
            if (a >= 0 && d >= 0) {
                const float targetLength = Distance(Vec(original, Word(original, 0x30) + size_t(bone) * 88 + 76),
                    Vec(original, Word(original, 0x30) + size_t(a) * 88 + 76));
                const float donorLength = Distance(Vec(donor, Word(donor, 0x30) + size_t(match->second) * 88 + 76),
                    Vec(donor, Word(donor, 0x30) + size_t(d) * 88 + 76));
                // Coincident pivots and tiny donor helper bones do not define limb proportions.
                const float ratio = donorLength > .01f ? targetLength / donorLength : 0;
                if (targetLength > .01f && ratio >= .1f && ratio <= 10) scale = ratio;
            }
            if (!std::isfinite(scale) || scale < .1f || scale > 10) return Fail(error, "Incompatible Nightborne limb proportions");
        }
        uint16_t interpolation = Half(original, track.at);
        for (size_t n = 0; n < borrowed.size(); ++n) {
            Keys selected = track.event ? Keys{} : rest;
            if (donorTrack) {
                if (!KeysAt(donor, donorTrack, track.key, borrowed[n], selected)) return Fail(error, "Invalid Nightborne action keys");
                if (!selected.values.empty()) {
                    if (selected.interpolation >= 2) return Fail(error, "Spline skeletal retargeting is unsupported");
                    if (selected.interpolation != interpolation) {
                        if (selected.times.size() > 4) {
                            for (uint32_t i = 0; i < oldCount; ++i)
                                if (keys[i].times.size() > 4) return Fail(error, "Nightborne interpolation would change authored Ethereal motion");
                            interpolation = selected.interpolation;
                        }
                    }
                    // Rotations use the shared bind axes directly; rebasing from an animated idle twists the joints.
                    if (offset == 16)
                        for (size_t at = 0; at < selected.values.size(); at += track.key) {
                            auto value = Vec(selected.values, at);
                            for (float& component : value) component *= scale;
                            std::memcpy(selected.values.data() + at, value.data(), 12);
                        }
                }
            }
            keys[destinations[n]] = std::move(selected);
        }
        HalfPut(b, track.at, interpolation);
        StoreTrack(b, track.at, track.key, keys);
    }
    for (uint32_t i : destinations) {
        // Donor array indexes cannot escape into the source model's sequence table.
        HalfPut(sequences, size_t(i) * 64 + 60, 65535);
        HalfPut(sequences, size_t(i) * 64 + 62, uint16_t(i));
        Put(sequences, size_t(i) * 64 + 12, (Word(sequences, size_t(i) * 64 + 12) & ~0x200040u) | 0x20);
    }
    Put(b, 0x1c, total); Put(b, 0x20, uint32_t(Append(b, sequences)));
    uint32_t maxId = 0;
    for (uint32_t i = 0; i < total; ++i) maxId = std::max(maxId, uint32_t(Half(sequences, size_t(i) * 64)));
    Bytes lookup(size_t(maxId + 1) * 2, 255);
    for (uint32_t i = 0; i < total; ++i) {
        const uint16_t id = Half(sequences, size_t(i) * 64);
        if (Half(lookup, size_t(id) * 2) == 65535 || !Half(sequences, size_t(i) * 64 + 2)) HalfPut(lookup, size_t(id) * 2, uint16_t(i));
    }
    Put(b, 0x24, maxId + 1); Put(b, 0x28, uint32_t(Append(b, lookup)));
    if (!Array(original, 0xf0, 40) || !Array(original, 0xf8, 2) || !Array(donor, 0xf0, 40))
        return Fail(error, "Invalid Ethereal attachment arrays");
    std::set<uint32_t> sockets;
    Bytes attachments(b.begin() + Word(original, 0xf4),
        b.begin() + Word(original, 0xf4) + size_t(Word(original, 0xf0)) * 40);
    for (uint32_t i = 0; i < Word(original, 0xf0); ++i) sockets.insert(Word(attachments, size_t(i) * 40));
    for (uint32_t i = 0; i < Word(donor, 0xf0); ++i) {
        const size_t at = Word(donor, 0xf4) + size_t(i) * 40;
        const uint32_t id = Word(donor, at);
        int32_t socketBone = Half(donor, at + 4);
        for (uint32_t depth = 0; socketBone >= 0 && !inverse.count(uint32_t(socketBone)); ++depth) {
            if (uint32_t(socketBone) >= Word(donor, 0x2c) || depth >= Word(donor, 0x2c))
                return Fail(error, "Invalid Nightborne socket ancestry");
            socketBone = parent(donor, uint32_t(socketBone));
        }
        const auto bone = socketBone < 0 ? inverse.end() : inverse.find(uint32_t(socketBone));
        if (sockets.count(id) || bone == inverse.end()) continue;
        if (id > 255) return Fail(error, "Unexpected Ethereal equipment socket ID");
        Bytes row(donor.begin() + at, donor.begin() + at + 40);
        HalfPut(row, 4, uint16_t(bone->second));
        const auto a = Vec(original, Word(original, 0x30) + size_t(bone->second) * 88 + 76);
        const auto d = Vec(donor, Word(donor, 0x30) + size_t(socketBone) * 88 + 76);
        auto position = Vec(row, 8);
        for (size_t axis = 0; axis < 3; ++axis) position[axis] += a[axis] - d[axis];
        std::memcpy(row.data() + 8, position.data(), 12);
        // Missing socket visibility has the engine's enabled default, with no borrowed external offsets.
        std::fill(row.begin() + 20, row.end(), 0);
        HalfPut(row, 22, 65535);
        attachments.insert(attachments.end(), row.begin(), row.end());
        sockets.insert(id);
    }
    const uint32_t attachmentCount = uint32_t(attachments.size() / 40);
    Put(b, 0xf0, attachmentCount); Put(b, 0xf4, uint32_t(Append(b, attachments)));
    Bytes socketLookup(size_t(std::max(Word(original, 0xf8), sockets.empty() ? 0u : *sockets.rbegin() + 1)) * 2, 255);
    for (uint32_t i = 0; i < attachmentCount; ++i) HalfPut(socketLookup, size_t(Word(attachments, size_t(i) * 40)) * 2, uint16_t(i));
    Put(b, 0xf8, uint32_t(socketLookup.size() / 2)); Put(b, 0xfc, uint32_t(Append(b, socketLookup)));
    if (!Array(b,0x100,36) || !Array(donor,0x100,36)) return Fail(error,"Invalid sheath event tables");
    Bytes events(b.begin()+Word(b,0x104),b.begin()+Word(b,0x104)+size_t(Word(b,0x100))*36);
    for (uint32_t e = 0; e < Word(donor,0x100); ++e) {
        const size_t from = Word(donor,0x104)+size_t(e)*36;
        const uint32_t tag = Word(donor,from);
        if (tag != 0x52485324 && tag != 0x4c485324) continue; // $SHR / $SHL exchange the drawn hand item.
        bool exists = false;
        for (size_t i = 0; i < events.size(); i += 36) exists = exists || Word(events,i) == tag;
        if (exists) continue;
        // The dispatcher's partial-arm filter walks parents, so the event must be below arm root 3/2, not on it.
        const int32_t targetBone = keyBone(b,tag == 0x52485324 ? 1 : 0);
        if (targetBone < 0) return Fail(error,"Missing Ethereal arm key for sheath event");
        Bytes record(donor.begin()+from,donor.begin()+from+36);
        HalfPut(record,8,uint16_t(targetBone));
        const auto position = Vec(b,Word(b,0x30)+size_t(targetBone)*88+76);
        std::memcpy(record.data()+12,position.data(),12);
        std::fill(record.begin()+24,record.end(),0); HalfPut(record,26,65535);
        std::vector<Keys> clocks(total);
        for (uint32_t i = 0; i < total; ++i) {
            const size_t action = size_t(i)*64;
            const uint16_t id = Half(sequences,action), variation = Half(sequences,action+2);
            if (id != 89 && id != 90) continue;
            for (uint32_t j = 0; j < Word(donor,0x1c); ++j) {
                const size_t clip = Word(donor,0x20)+size_t(j)*64;
                if (Half(donor,clip) == id && Half(donor,clip+2) == variation &&
                    !KeysAt(donor,from+24,0,j,clocks[i])) return Fail(error,"Invalid Nightborne sheath event clock");
            }
        }
        const size_t entry = Append(b,record);
        StoreTrack(b,entry+24,0,clocks);
        events.insert(events.end(),b.begin()+entry,b.begin()+entry+36);
    }
    Put(b,0x100,uint32_t(events.size()/36)); Put(b,0x104,uint32_t(Append(b,events)));
    return true;
}

inline void Chunk(Bytes& out, const char* tag, const Bytes& payload) {
    const size_t at = out.size(); out.resize(at + 8);
    std::memcpy(out.data() + at, tag, 4); Put(out, at + 4, uint32_t(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
}
inline bool Prepare(const Bytes& original, const Bytes& donorPrepared, const ModelFiles& donorFiles,
                    const Reader& read, Bytes& prepared, ModelFiles& files, std::string& error, bool borrowMotion = true) {
    try {
        ModelFiles graph;
        if (!InspectModel(original, graph, error) || graph.skeleton) return Fail(error, "Ethereal body must own its audited embedded rig");
        Bytes b;
        for (size_t at = 0; at < original.size();) {
            if (!Range(original, at, 8) || !Range(original, at + 8, Word(original, at + 4))) return Fail(error, "Invalid Ethereal model chunk");
            const size_t size = Word(original, at + 4);
            if (Word(original, at) == 0x3132444d) b.assign(original.begin() + at + 8, original.begin() + at + 8 + size);
            at += size + 8;
        }
        if (b.size() < 0x130) return Fail(error, "Missing Ethereal model payload");
        const uint32_t emitters = Word(b, 0x128);
        Bytes donor = donorPrepared;
        if (!Embed(b, graph, read, true, false, error)) return false;
        if (borrowMotion && (!Embed(donor, donorFiles, read, false, false, error) || !Complete(b, donor, error))) return false;
        Bytes wrapped, ids;
        Chunk(wrapped, "MD21", b);
        ids.resize(graph.skins.size() * 4); for (size_t i = 0; i < graph.skins.size(); ++i) Put(ids, i * 4, graph.skins[i]);
        Chunk(wrapped, "SFID", ids);
        ids.resize(graph.textures.size() * 4); for (size_t i = 0; i < graph.textures.size(); ++i) Put(ids, i * 4, graph.textures[i]);
        Chunk(wrapped, "TXID", ids);
        if (!PrepareModel(wrapped, prepared, files, error, nullptr, false)) return false;
        if (Word(prepared, 0x128) != emitters) return Fail(error, "Native preparation would discard required Ethereal glow emitters");
        return true;
    } catch (const std::exception& exception) { prepared.clear(); error = exception.what(); return false; }
}
}
#endif
