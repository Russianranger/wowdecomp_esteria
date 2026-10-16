#ifndef ENGINE_SOURCE_GX_CGXDEVICED3D11_SHADERTRANSLATOR_H
#define ENGINE_SOURCE_GX_CGXDEVICED3D11_SHADERTRANSLATOR_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Translates D3D9 SM2.0/SM3.0 shader bytecode (the payload of a .bls) to HLSL source
// that compiles for vs_5_0 / ps_5_0. SM1.x is not supported.
//
// Constants live in a single cbuffer b0 (float4 c[256]), the same layout as
// ShadowConstants. Samplers s# map to Texture2D/TextureCube/Texture3D t# + SamplerState s#.
struct D3d9ShaderInfo {
    bool m_isPixel = false;
    uint32_t m_major = 0;
    uint32_t m_minor = 0;

    struct Input {
        uint32_t m_usage;       // D3DDECLUSAGE
        uint32_t m_usageIndex;
        uint32_t m_reg;
    };

    std::vector<Input> m_inputs;  // vertex shader attribute inputs
    uint32_t m_samplerMask = 0;   // bit per sampler stage that is declared
    uint32_t m_failOp = 0;
    uint32_t m_samplerTypes[16] = {};  // 2 = 2D, 3 = cube, 4 = volume
};

const char* D3d9UsageName(uint32_t usage);
bool D3d9ShaderToHlsl(const uint8_t* code, size_t size, std::string& hlsl, D3d9ShaderInfo& info);

#endif
