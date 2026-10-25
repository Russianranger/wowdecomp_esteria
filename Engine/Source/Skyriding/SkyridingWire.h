#ifndef ESTERIA_SKYRIDING_WIRE_H
#define ESTERIA_SKYRIDING_WIRE_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include "SkyridingPhysics.h"
#include "SkyridingResources.h"

// Shared verbatim between the client (Engine/Source/Skyriding/) and the server (src/server/shared/Skyriding/).
// Skyriding protocol, version 1 (docs/SKYRIDING_PROTOCOL.md). Two opcodes; every payload starts with a subtype byte.
// All integers and floats little endian, written field by field (never a raw struct).

namespace SkyridingWire {

constexpr uint32_t ClientOpcode = 0x527;  // CMSG_ESTERIA_SKYRIDE
constexpr uint32_t ServerOpcode = 0x528;  // SMSG_ESTERIA_SKYRIDE
constexpr uint8_t Version = 1;

enum class ClientMsg : uint8_t { Hello = 1, Ability = 2, Style = 3 };
enum class ServerMsg : uint8_t { Begin = 1, End = 2, AbilityResult = 3, Resources = 4, Correction = 5 };
enum class Style : uint8_t { Dynamic = 0, Steady = 1 };
enum class EndReason : uint8_t { Dismount = 1, Area = 2, Teleport = 3, Death = 4, Style = 5, Disabled = 6, Vehicle = 7 };

class Writer {
    public:
    Writer(uint8_t* out, size_t capacity) : m_out(out), m_capacity(capacity) {}
    Writer& U8(uint8_t v) { return Raw(&v, 1); }
    Writer& U16(uint16_t v) { uint8_t b[2] = { uint8_t(v), uint8_t(v >> 8) }; return Raw(b, 2); }
    Writer& U32(uint32_t v) { uint8_t b[4] = { uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24) }; return Raw(b, 4); }
    Writer& F32(float v) { uint32_t u; std::memcpy(&u, &v, 4); return U32(u); }
    size_t Size() const { return m_ok ? m_size : 0; }

    private:
    Writer& Raw(const uint8_t* b, size_t n) {
        if (m_size + n > m_capacity) m_ok = false;
        else { std::memcpy(m_out + m_size, b, n); m_size += n; }
        return *this;
    }
    uint8_t* m_out;
    size_t m_capacity, m_size = 0;
    bool m_ok = true;
};

class Reader {
    public:
    Reader(const uint8_t* in, size_t size) : m_in(in), m_size(size) {}
    bool U8(uint8_t& v) { return Raw(&v, 1); }
    bool U16(uint16_t& v) { uint8_t b[2]; if (!Raw(b, 2)) return false; v = uint16_t(b[0] | b[1] << 8); return true; }
    bool U32(uint32_t& v) {
        uint8_t b[4];
        if (!Raw(b, 4)) return false;
        v = uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
        return true;
    }
    bool F32(float& v) { uint32_t u; if (!U32(u)) return false; std::memcpy(&v, &u, 4); return true; }
    bool Done() const { return m_pos == m_size; }

    private:
    bool Raw(uint8_t* b, size_t n) {
        if (m_pos + n > m_size) return false;
        std::memcpy(b, m_in + m_pos, n);
        m_pos += n;
        return true;
    }
    const uint8_t* m_in;
    size_t m_size, m_pos = 0;
};

// ---- client -> server ----------------------------------------------------------------------------------------------

struct Hello { uint8_t version = Version; Style style = Style::Dynamic; };
struct Ability { Skyriding::Command command = Skyriding::Command::None; uint16_t sequence = 0; };

constexpr size_t MaxClientSize = 8;
constexpr size_t MaxServerSize = 16 + Skyriding::Profile::FieldCount * 4;

inline size_t Write(const Hello& m, uint8_t* out, size_t cap) {
    return Writer(out, cap).U8(uint8_t(ClientMsg::Hello)).U8(m.version).U8(uint8_t(m.style)).Size();
}
inline size_t Write(const Ability& m, uint8_t* out, size_t cap) {
    return Writer(out, cap).U8(uint8_t(ClientMsg::Ability)).U8(uint8_t(m.command)).U16(m.sequence).Size();
}
inline size_t WriteStyle(Style style, uint8_t* out, size_t cap) {
    return Writer(out, cap).U8(uint8_t(ClientMsg::Style)).U8(uint8_t(style)).Size();
}

// Reads the subtype; the matching Read* below then reads the rest and checks nothing is left over.
inline bool ReadClientType(Reader& r, ClientMsg& type) {
    uint8_t t;
    if (!r.U8(t) || t < uint8_t(ClientMsg::Hello) || t > uint8_t(ClientMsg::Style)) return false;
    type = ClientMsg(t);
    return true;
}
inline bool Read(Reader& r, Hello& m) {
    uint8_t style;
    if (!r.U8(m.version) || !r.U8(style) || style > uint8_t(Style::Steady) || !r.Done()) return false;
    m.style = Style(style);
    return true;
}
inline bool Read(Reader& r, Ability& m) {
    uint8_t c;
    if (!r.U8(c) || !r.U16(m.sequence) || !r.Done() || c == 0 || c >= Skyriding::CommandCount) return false;
    m.command = Skyriding::Command(c);
    return true;
}
inline bool ReadStyle(Reader& r, Style& style) {
    uint8_t s;
    if (!r.U8(s) || s > uint8_t(Style::Steady) || !r.Done()) return false;
    style = Style(s);
    return true;
}

// ---- server -> client ----------------------------------------------------------------------------------------------

struct ResourceState {
    Skyriding::ResourceProfile profile = Skyriding::ResourceProfile::Charges;
    uint8_t charges = 0, maxCharges = 0;
    uint32_t nextChargeMs = 0;
};

struct Begin { ResourceState resources; Skyriding::Profile physics; };
struct AbilityReply {
    Skyriding::Command command = Skyriding::Command::None;
    uint16_t sequence = 0;
    Skyriding::AbilityResult result = Skyriding::AbilityResult::Ok;
    uint32_t cooldownMs = 0;
    ResourceState resources;
};

inline Writer& Put(Writer& w, const ResourceState& s) {
    return w.U8(uint8_t(s.profile)).U8(s.charges).U8(s.maxCharges).U32(s.nextChargeMs);
}
inline bool Get(Reader& r, ResourceState& s) {
    uint8_t profile;
    if (!r.U8(profile) || profile > uint8_t(Skyriding::ResourceProfile::Vigor) || !r.U8(s.charges) || !r.U8(s.maxCharges)
        || !r.U32(s.nextChargeMs) || s.maxCharges > 20 || s.charges > s.maxCharges) {
        return false;
    }
    s.profile = Skyriding::ResourceProfile(profile);
    return true;
}

inline size_t Write(const Begin& m, uint8_t* out, size_t cap) {
    Writer w(out, cap);
    w.U8(uint8_t(ServerMsg::Begin)).U8(Version).U8(uint8_t(Skyriding::Profile::FieldCount));
    Put(w, m.resources);
    for (uint32_t i = 0; i < Skyriding::Profile::FieldCount; ++i) w.F32(m.physics.Fields()[i]);
    return w.Size();
}
inline size_t WriteEnd(EndReason reason, uint8_t* out, size_t cap) {
    return Writer(out, cap).U8(uint8_t(ServerMsg::End)).U8(uint8_t(reason)).Size();
}
inline size_t Write(const AbilityReply& m, uint8_t* out, size_t cap) {
    Writer w(out, cap);
    w.U8(uint8_t(ServerMsg::AbilityResult)).U8(uint8_t(m.command)).U16(m.sequence).U8(uint8_t(m.result)).U32(m.cooldownMs);
    return Put(w, m.resources).Size();
}
inline size_t Write(const ResourceState& m, uint8_t* out, size_t cap) {
    Writer w(out, cap);
    w.U8(uint8_t(ServerMsg::Resources));
    return Put(w, m).Size();
}
inline size_t WriteCorrection(uint8_t* out, size_t cap) {
    return Writer(out, cap).U8(uint8_t(ServerMsg::Correction)).Size();
}

inline bool ReadServerType(Reader& r, ServerMsg& type) {
    uint8_t t;
    if (!r.U8(t) || t < uint8_t(ServerMsg::Begin) || t > uint8_t(ServerMsg::Correction)) return false;
    type = ServerMsg(t);
    return true;
}
inline bool Read(Reader& r, Begin& m) {
    uint8_t version, fields;
    if (!r.U8(version) || version != Version || !r.U8(fields) || fields != Skyriding::Profile::FieldCount
        || !Get(r, m.resources)) {
        return false;
    }
    for (uint32_t i = 0; i < Skyriding::Profile::FieldCount; ++i) {
        if (!r.F32(m.physics.Fields()[i])) return false;
    }
    return r.Done() && m.physics.Valid();
}
inline bool ReadEnd(Reader& r, EndReason& reason) {
    uint8_t v;
    if (!r.U8(v) || v < uint8_t(EndReason::Dismount) || v > uint8_t(EndReason::Vehicle) || !r.Done()) return false;
    reason = EndReason(v);
    return true;
}
inline bool Read(Reader& r, AbilityReply& m) {
    uint8_t c, result;
    if (!r.U8(c) || c == 0 || c >= Skyriding::CommandCount || !r.U16(m.sequence) || !r.U8(result)
        || result > uint8_t(Skyriding::AbilityResult::Disabled) || !r.U32(m.cooldownMs) || !Get(r, m.resources) || !r.Done()) {
        return false;
    }
    m.command = Skyriding::Command(c);
    m.result = Skyriding::AbilityResult(result);
    return true;
}
inline bool Read(Reader& r, ResourceState& m) { return Get(r, m) && r.Done(); }

}  // namespace SkyridingWire

#endif
