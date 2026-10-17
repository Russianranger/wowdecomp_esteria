#include "Engine/Source/Retail/RetailShader.h"
#include "Engine/Source/Gx/CGxDeviceD3d11/ShaderTranslator.h"
#include <windows.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) return 2;
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
    auto word = [&](size_t p) { uint32_t v; std::memcpy(&v, bytes.data() + p, 4); return v; };
    if (bytes.size() < 12 || word(0) != 0x47585348 || word(4) != 0x10003 || word(8) != 16) return 1;
    size_t p = 12;
    for (unsigned i = 0; i < 16; ++i) {
        if (p > bytes.size() || bytes.size() - p < 16) return 1;
        const auto size = word(p + 12); p += 16;
        if (size > bytes.size() - p) return 1;
        std::vector<uint8_t> source(bytes.begin() + p, bytes.begin() + p + size), patched;
        std::string error, hlsl; D3d9ShaderInfo info;
        if (!Retail::AddGlowLayer(source, patched, error) ||
            !D3d9ShaderToHlsl(patched.data(), patched.size(), hlsl, info) || !(info.m_samplerMask & 4)) {
            std::fprintf(stderr, "Permutation %u: %s, opcode %u\n", i, error.c_str(), info.m_failOp); return 1;
        }
        // Optional: keep the patched token streams for the offline Vulkan translator.
        if (argc == 3) {
            char name[32]; std::snprintf(name, sizeof(name), "RetailGlow_%02u.bin", i);
            std::ofstream(std::string(argv[2]) + "/" + name, std::ios::binary).write(reinterpret_cast<const char*>(patched.data()), patched.size());
        }
        ID3DBlob* code = nullptr; ID3DBlob* errors = nullptr;
        const auto result = D3DCompile(hlsl.data(), hlsl.size(), "RetailGlow", nullptr, nullptr, "main", "ps_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
        if (FAILED(result)) std::fprintf(stderr, "Permutation %u: %s\n", i, errors ? static_cast<const char*>(errors->GetBufferPointer()) : "compile failed");
        if (code) code->Release(); if (errors) errors->Release();
        if (FAILED(result)) return 1;
        p = (p + size + 3) & ~size_t(3);
    }
    std::puts("All 16 native glow shader permutations translated and compiled for D3D11.");
    return p == bytes.size() ? 0 : 1;
}
