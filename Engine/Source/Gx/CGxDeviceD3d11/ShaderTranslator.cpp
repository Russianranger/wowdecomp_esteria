#include "Engine/Source/Gx/CGxDeviceD3d11/ShaderTranslator.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

namespace {

enum {
    RT_TEMP = 0,
    RT_INPUT = 1,
    RT_CONST = 2,
    RT_ADDR = 3,  // also 't' in pixel shaders
    RT_RASTOUT = 4,
    RT_ATTROUT = 5,
    RT_OUTPUT = 6,  // oT# in SM2, o# in SM3
    RT_CONSTINT = 7,
    RT_COLOROUT = 8,
    RT_DEPTHOUT = 9,
    RT_SAMPLER = 10,
    RT_CONSTBOOL = 14,
    RT_LOOP = 15,
    RT_MISC = 17,
};

enum {
    OP_NOP = 0, OP_MOV = 1, OP_ADD = 2, OP_SUB = 3, OP_MAD = 4, OP_MUL = 5, OP_RCP = 6, OP_RSQ = 7,
    OP_DP3 = 8, OP_DP4 = 9, OP_MIN = 10, OP_MAX = 11, OP_SLT = 12, OP_SGE = 13, OP_EXP = 14, OP_LOG = 15,
    OP_LIT = 16, OP_DST = 17, OP_LRP = 18, OP_FRC = 19, OP_M4X4 = 20, OP_M4X3 = 21, OP_M3X4 = 22, OP_M3X3 = 23, OP_M3X2 = 24,
    OP_LOOP = 27, OP_ENDLOOP = 29, OP_DCL = 31, OP_POW = 32, OP_CRS = 33, OP_SGN = 34, OP_ABS = 35,
    OP_NRM = 36, OP_SINCOS = 37, OP_IF = 40, OP_IFC = 41, OP_ELSE = 42, OP_ENDIF = 43, OP_BREAK = 44,
    OP_BREAKC = 45, OP_MOVA = 46, OP_DEFB = 47, OP_DEFI = 48, OP_TEXKILL = 65, OP_TEX = 66, OP_EXPP = 78,
    OP_LOGP = 79, OP_DEF = 81, OP_CMP = 88, OP_DP2ADD = 90, OP_TEXLDL = 95,
    OP_COMMENT = 0xFFFE, OP_END = 0xFFFF,
};

struct Reg {
    uint32_t m_type;
    uint32_t m_num;
    bool m_relative;
    uint32_t m_token;
};

Reg DecodeReg(uint32_t token) {
    Reg r;
    r.m_type = ((token >> 28) & 7) | ((token >> 8) & 0x18);
    r.m_num = token & 0x7FF;
    r.m_relative = (token >> 13) & 1;
    r.m_token = token;
    return r;
}

std::string Fmt(const char* format, ...);

// D3D11 links vertex and pixel shaders by interpolant register, so every translated shader (and the
// fixed function shaders) declares the same full set of interpolants in the same order.
const char* const kInterpolants[11] = { "COLOR0", "COLOR1", "TEXCOORD0", "TEXCOORD1", "TEXCOORD2", "TEXCOORD3", "TEXCOORD4", "TEXCOORD5", "TEXCOORD6", "TEXCOORD7", "FOG0" };

int32_t InterpolantIndex(const std::string& semantic) {
    for (int32_t i = 0; i < 11; i++) {
        if (semantic == kInterpolants[i]) {
            return i;
        }
    }

    return -1;
}

std::string InterpolantDecls() {
    std::string h;

    for (int32_t i = 0; i < 11; i++) {
        h += Fmt("  float4 gx_i%d : %s;\n", i, kInterpolants[i]);
    }

    return h;
}

class Translator {
    public:
    Translator(const uint8_t* code, size_t size, D3d9ShaderInfo& info) : m_tokens(reinterpret_cast<const uint32_t*>(code)), m_count(size / 4), m_info(info) {}

    bool Run(std::string& out);

    private:
    const uint32_t* m_tokens;
    size_t m_count;
    D3d9ShaderInfo& m_info;

    bool m_pixel = false;
    uint32_t m_major = 0;
    int32_t m_maxTemp = -1;
    std::string m_body;
    std::string m_defs;
    std::set<uint32_t> m_defined;
    std::set<uint32_t> m_definedInt;
    std::set<uint32_t> m_definedBool;
    std::set<uint32_t> m_usedInt;
    std::set<uint32_t> m_usedBool;
    int32_t m_indent = 1;
    uint32_t m_loopId = 0;
    bool m_usesAddr = false;
    bool m_usesLoop = false;
    bool m_usesFace = false;
    bool m_usesPos = false;

    struct Interp {
        std::string m_name;      // local variable (v0, o1, ...)
        std::string m_semantic;  // TEXCOORD3
        bool m_position = false;
    };

    std::map<uint32_t, Interp> m_psInputs;     // pixel: register -> input
    std::map<uint32_t, Interp> m_vsOutputs;    // vertex SM3: register -> output
    std::set<std::string> m_vsOutUsed;         // vertex SM2: implicit outputs written
    std::set<uint32_t> m_psOutputs;
    bool m_psDepth = false;

    void Line(const std::string& text) { m_body += std::string(m_indent * 2, ' ') + text + "\n"; }

    static const char* UsageName(uint32_t usage) {
        static const char* names[] = { "POSITION", "BLENDWEIGHT", "BLENDINDICES", "NORMAL", "PSIZE", "TEXCOORD", "TANGENT", "BINORMAL", "TESSFACTOR", "POSITIONT", "COLOR", "FOG", "DEPTH", "SAMPLE" };
        return usage < 14 ? names[usage] : "TEXCOORD";
    }

    std::string RegName(const Reg& r, bool forWrite);
    std::string Swizzle(uint32_t token);
    std::string Src(const uint32_t*& p, bool allowRel = true);
    std::string SrcRaw(const Reg& r, const uint32_t*& p);
    void Dest(const uint32_t*& p, std::string& target, std::string& mask, uint32_t& mod, int32_t& shift);
    void Store(const std::string& target, const std::string& mask, uint32_t mod, int32_t shift, const std::string& expr, bool scalar);
    std::string Float(float f);
    void NoteTemp(const Reg& r) {
        if (r.m_type == RT_TEMP && static_cast<int32_t>(r.m_num) > m_maxTemp) {
            m_maxTemp = r.m_num;
        }
    }
};

std::string Translator::Float(float f) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.9g", f);
    if (!strpbrk(buf, ".en") ) {
        strcat(buf, ".0");
    }
    return buf;
}

std::string Translator::Swizzle(uint32_t token) {
    static const char comps[] = "xyzw";
    uint32_t s = (token >> 16) & 0xFF;
    char out[6] = { '.', comps[s & 3], comps[(s >> 2) & 3], comps[(s >> 4) & 3], comps[(s >> 6) & 3], 0 };
    return out;
}

std::string Translator::RegName(const Reg& r, bool forWrite) {
    char buf[48];

    switch (r.m_type) {
        case RT_TEMP:
            snprintf(buf, sizeof(buf), "r%u", r.m_num);
            return buf;

        case RT_INPUT:
            snprintf(buf, sizeof(buf), "v%u", r.m_num);
            return buf;

        case RT_CONST:
            snprintf(buf, sizeof(buf), "c[%u]", r.m_num);
            return buf;

        case RT_ADDR:
            if (m_pixel) {
                snprintf(buf, sizeof(buf), "t%u", r.m_num);
            } else {
                m_usesAddr = true;
                snprintf(buf, sizeof(buf), "a%u", r.m_num);
            }
            return buf;

        case RT_RASTOUT:
            if (r.m_num == 0) {
                return "oPos";
            }
            if (r.m_num == 1) {
                m_vsOutUsed.insert("oFog");
                return "oFog";
            }
            return "oPts";

        case RT_ATTROUT:
            snprintf(buf, sizeof(buf), "oD%u", r.m_num);
            m_vsOutUsed.insert(buf);
            return buf;

        case RT_OUTPUT:
            snprintf(buf, sizeof(buf), m_major >= 3 ? "o%u" : "oT%u", r.m_num);
            if (m_major < 3) {
                m_vsOutUsed.insert(buf);
            }
            return buf;

        case RT_COLOROUT:
            snprintf(buf, sizeof(buf), "oC%u", r.m_num);
            if (forWrite) {
                m_psOutputs.insert(r.m_num);
            }
            return buf;

        case RT_DEPTHOUT:
            m_psDepth = true;
            return "oDepth";

        case RT_LOOP:
            m_usesLoop = true;
            return "aL";

        case RT_MISC:
            if (r.m_num == 0) {
                m_usesPos = true;
                return "vPos";
            }
            m_usesFace = true;
            return "vFace";

        case RT_CONSTINT:
            m_usedInt.insert(r.m_num);
            snprintf(buf, sizeof(buf), "i%u", r.m_num);
            return buf;

        case RT_CONSTBOOL:
            m_usedBool.insert(r.m_num);
            snprintf(buf, sizeof(buf), "b%u", r.m_num);
            return buf;

        default:
            return "unsupported_reg";
    }
}

// Reads one source operand (and its relative-address token if present).
std::string Translator::Src(const uint32_t*& p, bool allowRel) {
    uint32_t token = *p++;
    Reg r = DecodeReg(token);
    (void)allowRel;
    return SrcRaw(r, p);
}

std::string Translator::SrcRaw(const Reg& r, const uint32_t*& p) {
    NoteTemp(r);

    std::string base;

    if (r.m_type == RT_CONST) {
        if (r.m_relative) {
            uint32_t relToken = *p++;
            Reg rel = DecodeReg(relToken);
            std::string idx;
            if (rel.m_type == RT_LOOP) {
                m_usesLoop = true;
                idx = "aL";
            } else {
                m_usesAddr = true;
                idx = Fmt("a%u", rel.m_num) + Swizzle(relToken).substr(0, 2);
            }
            base = Fmt("c[%u + %s]", r.m_num, idx.c_str());
        } else if (m_defined.count(r.m_num)) {
            base = Fmt("cd%u", r.m_num);
        } else {
            base = Fmt("c[%u]", r.m_num);
        }
    } else {
        if (r.m_relative) {
            // relative addressing of non-constant registers (SM3 input/output arrays) is not used by WoW
            p++;
        }
        base = RegName(r, false);
    }

    std::string e = base + Swizzle(r.m_token);
    uint32_t mod = (r.m_token >> 24) & 0xF;

    switch (mod) {
        case 1: e = "(-" + e + ")"; break;
        case 2: e = "(" + e + " - 0.5)"; break;
        case 3: e = "(-(" + e + " - 0.5))"; break;
        case 4: e = "(2.0 * (" + e + " - 0.5))"; break;
        case 5: e = "(-2.0 * (" + e + " - 0.5))"; break;
        case 6: e = "(1.0 - " + e + ")"; break;
        case 7: e = "(" + e + " * 2.0)"; break;
        case 8: e = "(-" + e + " * 2.0)"; break;
        case 9: e = "(" + e + " / " + e + ".z)"; break;
        case 10: e = "(" + e + " / " + e + ".w)"; break;
        case 11: e = "abs(" + e + ")"; break;
        case 12: e = "(-abs(" + e + "))"; break;
        case 13: e = "(1.0 - " + e + ")"; break;
        default: break;
    }

    return e;
}

void Translator::Dest(const uint32_t*& p, std::string& target, std::string& mask, uint32_t& mod, int32_t& shift) {
    uint32_t token = *p++;
    Reg r = DecodeReg(token);
    NoteTemp(r);

    if (r.m_relative) {
        p++;  // relative dest, unsupported; skip the address token
    }

    target = RegName(r, true);

    static const char comps[] = "xyzw";
    mask.clear();
    for (uint32_t i = 0; i < 4; i++) {
        if (token & (1u << (16 + i))) {
            mask += comps[i];
        }
    }

    mod = (token >> 20) & 0xF;
    shift = static_cast<int32_t>((token >> 24) & 0xF);
    if (shift & 8) {
        shift -= 16;
    }

    // SM2 colour interpolants are clamped to [0,1]
    if (!m_pixel && m_major < 3 && r.m_type == RT_ATTROUT) {
        mod |= 1;
    }
}

void Translator::Store(const std::string& target, const std::string& mask, uint32_t mod, int32_t shift, const std::string& expr, bool scalar) {
    if (mask.empty()) {
        return;
    }

    std::string e = expr;

    if (shift > 0) {
        e = Fmt("((%s) * %d.0)", e.c_str(), 1 << shift);
    } else if (shift < 0) {
        e = Fmt("((%s) / %d.0)", e.c_str(), 1 << -shift);
    }

    if (mod & 1) {
        e = "saturate(" + e + ")";
    }

    if (scalar) {
        Line(Fmt("%s.%s = %s;", target.c_str(), mask.c_str(), e.c_str()));
    } else {
        Line(Fmt("%s.%s = (%s).%s;", target.c_str(), mask.c_str(), e.c_str(), mask.c_str()));
    }
}

bool Translator::Run(std::string& out) {
    if (m_count < 2) {
        return false;
    }

    uint32_t version = m_tokens[0];
    uint32_t type = version >> 16;

    if (type != 0xFFFF && type != 0xFFFE) {
        return false;
    }

    m_pixel = type == 0xFFFF;
    m_major = (version >> 8) & 0xFF;
    m_info.m_isPixel = m_pixel;
    m_info.m_major = m_major;
    m_info.m_minor = version & 0xFF;

    if (m_major < 2) {
        return false;
    }

    std::string samplers;

    const uint32_t* p = m_tokens + 1;
    const uint32_t* end = m_tokens + m_count;

    while (p < end) {
        uint32_t tok = *p;
        uint32_t op = tok & 0xFFFF;

        if (op == OP_COMMENT) {
            p += 1 + ((tok >> 16) & 0x7FFF);
            continue;
        }

        if (op == OP_END) {
            break;
        }

        uint32_t length = (tok >> 24) & 0xF;
        uint32_t control = (tok >> 16) & 0xFF;
        const uint32_t* next = p + 1 + length;

        if (next > end) {
            return false;
        }

        p++;

        switch (op) {
            case OP_NOP:
                break;

            case OP_DCL: {
                uint32_t usageTok = *p++;
                uint32_t regTok = *p++;
                Reg r = DecodeReg(regTok);
                uint32_t usage = usageTok & 0x1F;
                uint32_t usageIndex = (usageTok >> 16) & 0xF;

                if (r.m_type == RT_SAMPLER) {
                    uint32_t texType = (usageTok >> 27) & 0xF;
                    if (r.m_num < 16) {
                        m_info.m_samplerMask |= 1u << r.m_num;
                        m_info.m_samplerTypes[r.m_num] = texType;
                    }
                    const char* tex = texType == 3 ? "TextureCube" : (texType == 4 ? "Texture3D" : "Texture2D");
                    samplers += Fmt("%s tex%u : register(t%u);\nSamplerState smp%u : register(s%u);\n", tex,r.m_num, r.m_num, r.m_num, r.m_num);
                } else if (!m_pixel && r.m_type == RT_INPUT) {
                    m_info.m_inputs.push_back({ usage, usageIndex, r.m_num });
                } else if (m_pixel && r.m_type == RT_INPUT) {
                    Interp in;
                    in.m_name = Fmt("v%u", r.m_num);
                    if (m_major >= 3) {
                        in.m_semantic = Fmt("%s%u", UsageName(usage), usageIndex);
                    } else {
                        in.m_semantic = Fmt("COLOR%u", r.m_num);
                    }
                    m_psInputs[(RT_INPUT << 8) | r.m_num] = in;
                } else if (m_pixel && r.m_type == RT_ADDR) {
                    Interp in;
                    in.m_name = Fmt("t%u", r.m_num);
                    in.m_semantic = Fmt("TEXCOORD%u", r.m_num);
                    m_psInputs[(RT_ADDR << 8) | r.m_num] = in;
                } else if (m_pixel && r.m_type == RT_MISC) {
                    if (r.m_num == 0) {
                        m_usesPos = true;
                    } else {
                        m_usesFace = true;
                    }
                } else if (!m_pixel && r.m_type == RT_OUTPUT && m_major >= 3) {
                    Interp o;
                    o.m_name = Fmt("o%u", r.m_num);
                    o.m_position = usage == 0 || usage == 9;
                    o.m_semantic = Fmt("%s%u", UsageName(usage), usageIndex);
                    m_vsOutputs[r.m_num] = o;
                }
                break;
            }

            case OP_DEF: {
                Reg r = DecodeReg(*p++);
                float f[4];
                memcpy(f, p, sizeof(f));
                p += 4;
                m_defined.insert(r.m_num);
                m_defs += Fmt("static const float4 cd%u = float4(%s, %s, %s, %s);\n", r.m_num, Float(f[0]).c_str(), Float(f[1]).c_str(), Float(f[2]).c_str(), Float(f[3]).c_str());
                break;
            }

            case OP_DEFI: {
                Reg r = DecodeReg(*p++);
                int32_t v[4];
                memcpy(v, p, sizeof(v));
                p += 4;
                m_definedInt.insert(r.m_num);
                m_defs += Fmt("static const int4 i%u = int4(%d, %d, %d, %d);\n", r.m_num, v[0], v[1], v[2], v[3]);
                break;
            }

            case OP_DEFB: {
                Reg r = DecodeReg(*p++);
                m_definedBool.insert(r.m_num);
                m_defs += Fmt("static const bool b%u = %s;\n", r.m_num, *p ? "true" : "false");
                p++;
                break;
            }

            case OP_MOV: case OP_RCP: case OP_RSQ: case OP_EXP: case OP_LOG: case OP_FRC: case OP_ABS: case OP_NRM:
            case OP_EXPP: case OP_LOGP: case OP_MOVA: case OP_SINCOS: case OP_LIT: {
                std::string t, m;
                uint32_t mod;
                int32_t sh;
                Dest(p, t, m, mod, sh);
                std::string a = Src(p);
                std::string e;
                bool scalar = false;

                switch (op) {
                    case OP_MOV: e = a; break;
                    case OP_RCP: e = "(1.0 / " + a + ".x)"; scalar = true; break;
                    case OP_RSQ: e = "rsqrt(abs(" + a + ".x))"; scalar = true; break;
                    case OP_EXP: case OP_EXPP: e = "exp2(" + a + ".x)"; scalar = true; break;
                    case OP_LOG: case OP_LOGP: e = "log2(abs(" + a + ".x))"; scalar = true; break;
                    case OP_FRC: e = "frac(" + a + ")"; break;
                    case OP_ABS: e = "abs(" + a + ")"; break;
                    case OP_NRM: e = "(" + a + " * rsqrt(dot(" + a + ".xyz, " + a + ".xyz)))"; break;
                    case OP_LIT: e = "float4(1.0, max(" + a + ".x, 0.0), (" + a + ".x > 0.0) ? pow(max(" + a + ".y, 0.0), clamp(" + a + ".w, -127.9961, 127.9961)) : 0.0, 1.0)"; break;
                    case OP_SINCOS: e = "float4(cos(" + a + ".x), sin(" + a + ".x), 0.0, 0.0)"; break;
                    case OP_MOVA:
                        // the address register is integer, written round-to-nearest
                        for (char comp : m) {
                            Line(Fmt("%s.%c = (int)floor((%s).%c + 0.5);", t.c_str(), comp, a.c_str(), comp));
                        }
                        e.clear();
                        break;
                }

                if (!e.empty()) {
                    Store(t, m, mod, sh, e, scalar);
                }
                break;
            }

            case OP_ADD: case OP_SUB: case OP_MUL: case OP_MIN: case OP_MAX: case OP_SLT: case OP_SGE: case OP_POW:
            case OP_DP3: case OP_DP4: case OP_CRS: case OP_DST: {
                std::string t, m;
                uint32_t mod;
                int32_t sh;
                Dest(p, t, m, mod, sh);
                std::string a = Src(p);
                std::string b = Src(p);
                std::string e;
                bool scalar = false;

                switch (op) {
                    case OP_ADD: e = a + " + " + b; break;
                    case OP_SUB: e = a + " - " + b; break;
                    case OP_MUL: e = a + " * " + b; break;
                    case OP_MIN: e = "min(" + a + ", " + b + ")"; break;
                    case OP_MAX: e = "max(" + a + ", " + b + ")"; break;
                    case OP_SLT: e = "(" + a + " < " + b + " ? 1.0 : 0.0)"; break;
                    case OP_SGE: e = "(" + a + " >= " + b + " ? 1.0 : 0.0)"; break;
                    case OP_POW: e = "pow(abs(" + a + ".x), " + b + ".x)"; scalar = true; break;
                    case OP_DP3: e = "dot(" + a + ".xyz, " + b + ".xyz)"; scalar = true; break;
                    case OP_DP4: e = "dot(" + a + ", " + b + ")"; scalar = true; break;
                    case OP_DST: e = "float4(1.0, " + a + ".y * " + b + ".y, " + a + ".z, " + b + ".w)"; break;
                    case OP_CRS: e = "float4(cross(" + a + ".xyz, " + b + ".xyz), 0.0)"; break;
                }

                Store(t, m, mod, sh, e, scalar);
                break;
            }

            case OP_MAD: case OP_LRP: case OP_CMP: case OP_DP2ADD: {
                std::string t, m;
                uint32_t mod;
                int32_t sh;
                Dest(p, t, m, mod, sh);
                std::string a = Src(p);
                std::string b = Src(p);
                std::string c = Src(p);
                std::string e;
                bool scalar = false;

                switch (op) {
                    case OP_MAD: e = a + " * " + b + " + " + c; break;
                    case OP_LRP: e = "lerp(" + c + ", " + b + ", " + a + ")"; break;
                    case OP_CMP: e = "((" + a + " >= 0.0) ? " + b + " : " + c + ")"; break;
                    case OP_DP2ADD: e = "(dot(" + a + ".xy, " + b + ".xy) + " + c + ".x)"; scalar = true; break;
                }

                Store(t, m, mod, sh, e, scalar);
                break;
            }

            case OP_TEX: case OP_TEXLDL: {
                std::string t, m;
                uint32_t mod;
                int32_t sh;
                Dest(p, t, m, mod, sh);
                std::string coord = Src(p);
                uint32_t samplerTok = *p++;
                Reg sr = DecodeReg(samplerTok);
                uint32_t n = sr.m_num;
                uint32_t texType = n < 16 ? m_info.m_samplerTypes[n] : 2;
                const char* dim = texType == 2 || texType == 0 ? "xy" : "xyz";
                std::string uv = "(" + coord + ")." + dim;
                std::string e;

                if (op == OP_TEXLDL) {
                    e = Fmt("tex%u.SampleLevel(smp%u, %s, (%s).w)", n, n, uv.c_str(), coord.c_str());
                } else if (control == 1) {
                    e = Fmt("tex%u.Sample(smp%u, (%s) / (%s).w)", n, n, uv.c_str(), coord.c_str());
                } else if (control == 2) {
                    e = Fmt("tex%u.SampleBias(smp%u, %s, (%s).w)", n, n, uv.c_str(), coord.c_str());
                } else {
                    e = Fmt("tex%u.Sample(smp%u, %s)", n, n, uv.c_str());
                }

                if (n < 16 && !(m_info.m_samplerMask & (1u << n))) {
                    // sampler used without a dcl; treat as 2D
                    m_info.m_samplerMask |= 1u << n;
                    m_info.m_samplerTypes[n] = 2;
                    samplers += Fmt("Texture2D tex%u : register(t%u);\nSamplerState smp%u : register(s%u);\n", n, n, n, n);
                }

                Store(t, m, mod, sh, e, false);
                break;
            }

            case OP_TEXKILL: {
                Reg r = DecodeReg(*p++);
                NoteTemp(r);
                std::string name = RegName(r, false);
                Line(Fmt("clip(%s.%s);", name.c_str(), m_major >= 3 ? "xyzw" : "xyz"));
                break;
            }

            case OP_IFC: case OP_BREAKC: {
                std::string a = Src(p);
                std::string b = Src(p);
                static const char* cmp[] = { "?", ">", "==", ">=", "<", "!=", "<=", "?" };
                const char* c = cmp[control & 7];
                if (op == OP_IFC) {
                    Line(Fmt("if ((%s).x %s (%s).x) {", a.c_str(), c, b.c_str()));
                    m_indent++;
                } else {
                    Line(Fmt("if ((%s).x %s (%s).x) break;", a.c_str(), c, b.c_str()));
                }
                break;
            }

            case OP_IF: {
                Reg r = DecodeReg(*p++);
                std::string name = RegName(r, false);
                Line(Fmt("if (%s) {", name.c_str()));
                m_indent++;
                break;
            }

            case OP_ELSE:
                m_indent--;
                Line("} else {");
                m_indent++;
                break;

            case OP_ENDIF:
                m_indent--;
                Line("}");
                break;

            case OP_BREAK:
                Line("break;");
                break;

            case OP_LOOP: {
                p++;  // aL
                Reg ir = DecodeReg(*p++);
                std::string iname = RegName(ir, false);
                m_usesLoop = true;
                uint32_t id = m_loopId++;
                Line(Fmt("int4 lc%u = %s;", id, iname.c_str()));
                Line(Fmt("[loop] for (int ln%u = 0; ln%u < lc%u.x; ln%u++) {", id, id, id, id));
                m_indent++;
                Line(Fmt("aL = lc%u.y + ln%u * lc%u.z;", id, id, id));
                break;
            }

            case OP_ENDLOOP:
                m_indent--;
                Line("}");
                break;

            default:
                m_info.m_failOp = op;
                // unsupported instruction: fail the whole shader so it never renders wrong silently
                return false;
        }

        p = next;
    }

    // assemble
    std::string h;
    h += "cbuffer Constants : register(b0) { float4 c[256]; };\n";
    // b1: device-emulated D3D9 render state. gx_alpha.x/y = alpha test enable / reference, gx_clip = user clip planes.
    h += "cbuffer Extra : register(b1) { float4 gx_alpha; float4 gx_clip[6]; };\n";
    h += samplers;
    h += m_defs;

    for (uint32_t i : m_usedInt) {
        if (!m_definedInt.count(i)) {
            h += Fmt("static const int4 i%u = int4(0, 0, 0, 0);\n", i);
        }
    }
    for (uint32_t i : m_usedBool) {
        if (!m_definedBool.count(i)) {
            h += Fmt("static const bool b%u = false;\n", i);
        }
    }

    std::string temps;
    for (int32_t i = 0; i <= m_maxTemp; i++) {
        temps += Fmt("  float4 r%d = 0;\n", i);
    }

    if (m_pixel) {
        h += "struct PSIn {\n  float4 pos : SV_Position;\n";
        h += InterpolantDecls();
        if (m_usesFace) {
            h += "  bool face : SV_IsFrontFace;\n";
        }
        h += "};\n";

        h += "struct PSOut {\n";
        m_psOutputs.insert(0);
        for (uint32_t i : m_psOutputs) {
            h += Fmt("  float4 oC%u : SV_Target%u;\n", i, i);
        }
        if (m_psDepth) {
            h += "  float oDepth : SV_Depth;\n";
        }
        h += "};\n";

        h += "PSOut main(PSIn IN) {\n";
        for (auto& kv : m_psInputs) {
            int32_t slot = InterpolantIndex(kv.second.m_semantic);

            if (slot >= 0) {
                h += Fmt("  float4 %s = IN.gx_i%d;\n", kv.second.m_name.c_str(), slot);
            } else {
                h += Fmt("  float4 %s = 0;\n", kv.second.m_name.c_str());
            }
        }
        if (m_usesPos) {
            h += "  float4 vPos = float4(floor(IN.pos.xy), IN.pos.zw);\n";
        }
        if (m_usesFace) {
            h += "  float4 vFace = IN.face ? 1.0 : -1.0;\n";
        }
        if (m_usesLoop) {
            h += "  int aL = 0;\n";
        }
        h += temps;
        for (uint32_t i : m_psOutputs) {
            h += Fmt("  float4 oC%u = 0;\n", i);
        }
        if (m_psDepth) {
            h += "  float4 oDepth = 0;\n";
        }
        h += m_body;
        h += "  [branch] if (gx_alpha.x > 0.5) clip(oC0.a - gx_alpha.y);\n";
        h += "  [branch] if (gx_alpha.z > 0.5) oC0 = float4(1.0, 0.0, 1.0, 1.0);\n";
        // debug: gx_alpha.w > 0 paints the first TEXCOORD input as colour
        for (auto& kv : m_psInputs) {
            if (kv.second.m_semantic == "TEXCOORD0") {
                h += Fmt("  [branch] if (gx_alpha.w > 0.5 && gx_alpha.w < 1.5) oC0 = float4(frac(%s.xy), 0.0, 1.0);\n", kv.second.m_name.c_str());
            } else if (kv.second.m_semantic == "TEXCOORD1") {
                h += Fmt("  [branch] if (gx_alpha.w > 3.5 && gx_alpha.w < 4.5) oC0 = float4(%s.xy, 0.0, 1.0);\n", kv.second.m_name.c_str());
            } else if (kv.second.m_semantic == "FOG0") {
                h += Fmt("  [branch] if (gx_alpha.w > 1.5 && gx_alpha.w < 2.5) oC0 = float4(%s.xxx, 1.0);\n", kv.second.m_name.c_str());
            } else if (kv.second.m_semantic == "COLOR0") {
                h += Fmt("  [branch] if (gx_alpha.w > 2.5 && gx_alpha.w < 3.5) oC0 = float4(%s.xyz, 1.0);\n", kv.second.m_name.c_str());
            }
        }
        h += "  PSOut OUT;\n";
        for (uint32_t i : m_psOutputs) {
            h += Fmt("  OUT.oC%u = oC%u;\n", i, i);
        }
        if (m_psDepth) {
            h += "  OUT.oDepth = oDepth.x;\n";
        }
        h += "  return OUT;\n}\n";
    } else {
        // vertex shader
        h += "struct VSIn {\n";
        for (auto& in : m_info.m_inputs) {
            h += Fmt("  float4 v%u : %s%u;\n", in.m_reg, UsageName(in.m_usage), in.m_usageIndex);
        }
        h += "};\n";

        struct Out {
            std::string m_name, m_semantic;
        };
        std::vector<Out> outs;

        if (m_major >= 3) {
            for (auto& kv : m_vsOutputs) {
                if (kv.second.m_position) {
                    outs.push_back({ kv.second.m_name, "SV_Position" });
                } else if (kv.second.m_semantic.compare(0, 5, "PSIZE") != 0) {
                    outs.push_back({ kv.second.m_name, kv.second.m_semantic });
                }
            }
        } else {
            outs.push_back({ "oPos", "SV_Position" });
            for (auto& n : m_vsOutUsed) {
                if (n == "oFog") {
                    outs.push_back({ n, "FOG0" });
                } else if (n[1] == 'D') {
                    outs.push_back({ n, Fmt("COLOR%c", n[2]) });
                } else {
                    outs.push_back({ n, Fmt("TEXCOORD%c", n[2]) });
                }
            }
        }

        h += "struct VSOut {\n  float4 gx_pos : SV_Position;\n";
        h += InterpolantDecls();
        h += "  float4 gx_cd0 : SV_ClipDistance0;\n  float2 gx_cd1 : SV_ClipDistance1;\n";
        h += "};\n";

        h += "VSOut main(VSIn IN) {\n";
        for (auto& in : m_info.m_inputs) {
            // D3DDECLTYPE_UBYTE4 (bone indices) arrives as R8G8B8A8_UNORM; D3D9 delivered 0..255
            h += Fmt("  float4 v%u = IN.v%u%s;\n", in.m_reg, in.m_reg, in.m_usage == 2 ? " * 255.0" : "");
        }
        if (m_usesAddr) {
            h += "  int4 a0 = 0;\n";
        }
        if (m_usesLoop) {
            h += "  int aL = 0;\n";
        }
        h += temps;
        h += "  float4 oPos = 0;\n  float4 oFog = 0;\n  float4 oPts = 0;\n";
        if (m_major >= 3) {
            for (auto& kv : m_vsOutputs) {
                h += Fmt("  float4 %s = 0;\n", kv.second.m_name.c_str());
            }
        } else {
            for (int i = 0; i < 2; i++) {
                h += Fmt("  float4 oD%d = 0;\n", i);
            }
            for (int i = 0; i < 8; i++) {
                h += Fmt("  float4 oT%d = 0;\n", i);
            }
        }
        h += m_body;
        h += "  VSOut OUT = (VSOut)0;\n";
        for (auto& o : outs) {
            if (o.m_semantic == "SV_Position") {
                h += Fmt("  OUT.gx_pos = %s;\n", o.m_name.c_str());
            } else {
                int32_t slot = InterpolantIndex(o.m_semantic);

                if (slot >= 0) {
                    h += Fmt("  OUT.gx_i%d = %s;\n", slot, o.m_name.c_str());
                }
            }
        }
        // user clip planes are applied to the clip space position, whichever register the shader wrote it to
        std::string posName = "oPos";

        for (auto& kv : m_vsOutputs) {
            if (kv.second.m_position) {
                posName = kv.second.m_name;
                break;
            }
        }

        h += Fmt("  OUT.gx_cd0 = float4(dot(gx_clip[0], %s), dot(gx_clip[1], %s), dot(gx_clip[2], %s), dot(gx_clip[3], %s));\n", posName.c_str(), posName.c_str(), posName.c_str(), posName.c_str());
        h += Fmt("  OUT.gx_cd1 = float2(dot(gx_clip[4], %s), dot(gx_clip[5], %s));\n", posName.c_str(), posName.c_str());
        h += "  return OUT;\n}\n";
    }

    out = h;
    return true;
}

std::string Fmt(const char* format, ...) {
    char buf[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    return buf;
}

}  // namespace

bool D3d9ShaderToHlsl(const uint8_t* code, size_t size, std::string& hlsl, D3d9ShaderInfo& info) {
    Translator t(code, size, info);
    return t.Run(hlsl);
}

const char* D3d9UsageName(uint32_t usage) {
    static const char* names[] = { "POSITION", "BLENDWEIGHT", "BLENDINDICES", "NORMAL", "PSIZE", "TEXCOORD", "TANGENT", "BINORMAL", "TESSFACTOR", "POSITIONT", "COLOR", "FOG", "DEPTH", "SAMPLE" };
    return usage < 14 ? names[usage] : "TEXCOORD";
}
