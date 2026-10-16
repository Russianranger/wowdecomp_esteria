#include <cstdint>
#include <cstring>
#include <initializer_list>
#include "Engine/Source/Gx/CGxDeviceD3d11/CGxDeviceD3d11.h"
#include "Engine/Source/Gx/CGxTex.h"
#include "Engine/Source/Gx/Gx.h"

template <class T>
static void Rel(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

// ---------------------------------------------------------------------------------------------
// Fixed function pipeline emulation.
//
// The engine uses the D3D9 fixed function pipeline when a draw has no vertex / pixel shader bound:
// world*view*projection transform, up to four lights, material colour, per stage texture
// coordinate generation + transforms, and an up to 8 stage texture cascade limited to the six
// combiner modes the engine ever selects (see s_texOp in the D3D9 backend). Everything is driven from
// constants so a single pair of shaders covers all states.

static const char* const kFixedFunctionVs = R"HLSL(
cbuffer FFVS : register(b2) {
    row_major float4x4 g_world;
    row_major float4x4 g_view;
    row_major float4x4 g_proj;
    row_major float4x4 g_texMat[8];
    float4 g_texCtl[8];      // x: tex gen (0 none, 1 camera position, 2 reflection, 3 camera normal), y: coord set, z: matrix dims (0 off, 2, 3), w: projected
    float4 g_matDiffuse;
    float4 g_matAmbient;
    float4 g_matEmissive;
    float4 g_globalAmbient;
    float4 g_lightPos[4];    // xyz position (w = 1) or direction (w = 0)
    float4 g_lightDiffuse[4];
    float4 g_lightAmbient[4];
    float4 g_lightAtten[4];  // constant, linear, quadratic, enabled
    float4 g_flags;          // x: lighting, y: fog, z: vertex has colour0
    float4 g_flags2;         // x: ambient from colour0, y: diffuse from colour0, w: emissive from colour0
    float4 g_fog;            // x: start, y: end
    float4 g_clipPlane[6];
};

struct VSIn {
    float4 pos : POSITION0;
    float4 nrm : NORMAL0;
    float4 col0 : COLOR0;
    float4 col1 : COLOR1;
    float4 tc0 : TEXCOORD0;
    float4 tc1 : TEXCOORD1;
    float4 tc2 : TEXCOORD2;
    float4 tc3 : TEXCOORD3;
    float4 tc4 : TEXCOORD4;
    float4 tc5 : TEXCOORD5;
    float4 tc6 : TEXCOORD6;
    float4 tc7 : TEXCOORD7;
};

struct VSOut {
    float4 pos : SV_Position;
    float4 col0 : COLOR0;
    float4 col1 : COLOR1;
    float4 tc0 : TEXCOORD0;
    float4 tc1 : TEXCOORD1;
    float4 tc2 : TEXCOORD2;
    float4 tc3 : TEXCOORD3;
    float4 tc4 : TEXCOORD4;
    float4 tc5 : TEXCOORD5;
    float4 tc6 : TEXCOORD6;
    float4 tc7 : TEXCOORD7;
    float4 fog : FOG0;
    float4 gx_cd0 : SV_ClipDistance0;
    float2 gx_cd1 : SV_ClipDistance1;
};

VSOut main(VSIn v) {
    VSOut o;
    float4 tcs[8] = { v.tc0, v.tc1, v.tc2, v.tc3, v.tc4, v.tc5, v.tc6, v.tc7 };

    float4 wp = mul(float4(v.pos.xyz, 1.0), g_world);
    float4 vp = mul(wp, g_view);
    o.pos = mul(vp, g_proj);

    float3 n = mul(mul(v.nrm.xyz, (float3x3)g_world), (float3x3)g_view);
    n = normalize(n);

    float4 vcol = g_flags.z > 0.5 ? v.col0 : float4(1.0, 1.0, 1.0, 1.0);
    float4 col = vcol;

    if (g_flags.x > 0.5) {
        float4 amb = g_flags2.x > 0.5 ? vcol : g_matAmbient;
        float4 dif = g_flags2.y > 0.5 ? vcol : g_matDiffuse;
        float4 emi = g_flags2.w > 0.5 ? vcol : g_matEmissive;
        float3 c = emi.rgb + amb.rgb * g_globalAmbient.rgb;

        [unroll] for (int i = 0; i < 4; i++) {
            if (g_lightAtten[i].w < 0.5) {
                continue;
            }

            float3 L;
            float att = 1.0;

            if (g_lightPos[i].w > 0.5) {
                float3 d = mul(float4(g_lightPos[i].xyz, 1.0), g_view).xyz - vp.xyz;
                float dist = length(d);
                L = d / max(dist, 0.0001);
                att = 1.0 / max(g_lightAtten[i].x + g_lightAtten[i].y * dist + g_lightAtten[i].z * dist * dist, 0.0001);
            } else {
                L = -normalize(mul(g_lightPos[i].xyz, (float3x3)g_view));
            }

            float ndl = max(dot(n, L), 0.0);
            c += amb.rgb * g_lightAmbient[i].rgb * att + dif.rgb * g_lightDiffuse[i].rgb * ndl * att;
        }

        col = float4(saturate(c), dif.a);
    }

    o.col0 = col;
    o.col1 = float4(0.0, 0.0, 0.0, 0.0);

    float4 outTc[8];

    [unroll] for (int s = 0; s < 8; s++) {
        float4 src = tcs[(uint)g_texCtl[s].y & 7];
        int mode = (int)g_texCtl[s].x;

        if (mode == 1) {
            src = float4(vp.xyz, 1.0);
        } else if (mode == 2) {
            src = float4(reflect(normalize(vp.xyz), n), 1.0);
        } else if (mode == 3) {
            src = float4(n, 1.0);
        }

        float4 t = src;
        int dims = (int)g_texCtl[s].z;

        if (dims != 0) {
            float4 i4 = dims == 2 ? float4(src.x, src.y, 1.0, 0.0) : float4(src.xyz, 1.0);
            t = mul(i4, g_texMat[s]);

            if (g_texCtl[s].w > 0.5) {
                t.xy /= t.z;
            }
        }

        outTc[s] = t;
    }

    o.tc0 = outTc[0];
    o.tc1 = outTc[1];
    o.tc2 = outTc[2];
    o.tc3 = outTc[3];
    o.tc4 = outTc[4];
    o.tc5 = outTc[5];
    o.tc6 = outTc[6];
    o.tc7 = outTc[7];

    float fog = saturate((g_fog.y - o.pos.w) / (g_fog.y - g_fog.x));
    o.fog = (g_flags.y > 0.5 ? fog : 1.0).xxxx;

    o.gx_cd0 = float4(dot(g_clipPlane[0], wp), dot(g_clipPlane[1], wp), dot(g_clipPlane[2], wp), dot(g_clipPlane[3], wp));
    o.gx_cd1 = float2(dot(g_clipPlane[4], wp), dot(g_clipPlane[5], wp));
    return o;
}
)HLSL";

static const char* const kFixedFunctionPs = R"HLSL(
cbuffer FFPS : register(b2) {
    uint4 g_stage[8];        // x: colour op + 1 (0 = stage and all later stages disabled), y: alpha op + 1
    float4 g_fogColor;       // w: fog enabled
    float4 g_alphaTest;      // x: enabled, y: reference
};

Texture2D texs[8] : register(t0);
SamplerState smps[8] : register(s0);

struct PSIn {
    float4 pos : SV_Position;
    float4 col0 : COLOR0;
    float4 col1 : COLOR1;
    float4 tc0 : TEXCOORD0;
    float4 tc1 : TEXCOORD1;
    float4 tc2 : TEXCOORD2;
    float4 tc3 : TEXCOORD3;
    float4 tc4 : TEXCOORD4;
    float4 tc5 : TEXCOORD5;
    float4 tc6 : TEXCOORD6;
    float4 tc7 : TEXCOORD7;
    float4 fog : FOG0;
};

float4 main(PSIn i) : SV_Target {
    float4 tc[8] = { i.tc0, i.tc1, i.tc2, i.tc3, i.tc4, i.tc5, i.tc6, i.tc7 };
    float4 diffuse = i.col0;
    float4 cur = diffuse;

    [unroll] for (uint s = 0; s < 8; s++) {
        if (g_stage[s].x == 0) {
            break;
        }

        float4 t = texs[s].Sample(smps[s], tc[s].xy);
        uint co = g_stage[s].x - 1;
        uint ao = g_stage[s].y - 1;
        float3 rgb;
        float a;

        if (co == 0) {
            rgb = t.rgb * cur.rgb;
        } else if (co == 1) {
            rgb = saturate(t.rgb * cur.rgb * 2.0);
        } else if (co == 2) {
            rgb = saturate(t.rgb + cur.rgb);
        } else if (co == 3) {
            rgb = cur.rgb;
        } else if (co == 4) {
            rgb = cur.rgb * cur.a + t.rgb * (1.0 - cur.a);
        } else {
            rgb = t.rgb * diffuse.a + cur.rgb * (1.0 - diffuse.a);
        }

        if (ao == 0) {
            a = t.a * cur.a;
        } else if (ao == 1) {
            a = saturate(t.a * cur.a * 2.0);
        } else if (ao == 2) {
            a = saturate(t.a + cur.a);
        } else if (ao == 3) {
            a = cur.a;
        } else if (ao == 4) {
            a = cur.a * cur.a + t.a * (1.0 - cur.a);
        } else {
            a = t.a * diffuse.a + cur.a * (1.0 - diffuse.a);
        }

        cur = float4(rgb, a);
    }

    if (g_fogColor.w > 0.5) {
        cur.rgb = lerp(g_fogColor.rgb, cur.rgb, saturate(i.fog.x));
    }

    if (g_alphaTest.x > 0.5) {
        clip(cur.a - g_alphaTest.y);
    }

    return cur;
}
)HLSL";

static const char* const kBlitVs = R"HLSL(
cbuffer Blit : register(b3) { float4 g_rect; };
struct O { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
O main(uint id : SV_VertexID) {
    float2 p = float2((id << 1) & 2, id & 2);
    O o;
    o.pos = float4(p * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    o.uv = lerp(g_rect.xy, g_rect.zw, p);
    return o;
}
)HLSL";

static const char* const kBlitPs = R"HLSL(
Texture2D src : register(t0);
SamplerState smp : register(s0);
struct O { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
float4 main(O i) : SV_Target { return src.Sample(smp, i.uv); }
)HLSL";

struct FFVSConstants {
    float world[16];
    float view[16];
    float proj[16];
    float texMat[8][16];
    float texCtl[8][4];
    float matDiffuse[4];
    float matAmbient[4];
    float matEmissive[4];
    float globalAmbient[4];
    float lightPos[4][4];
    float lightDiffuse[4][4];
    float lightAmbient[4][4];
    float lightAtten[4][4];
    float flags[4];
    float flags2[4];
    float fog[4];
    float clipPlane[6][4];
};

struct FFPSConstants {
    uint32_t stage[8][4];
    float fogColor[4];
    float alphaTest[4];
};

static_assert(sizeof(FFVSConstants) % 16 == 0, "constant buffer size");
static_assert(sizeof(FFPSConstants) % 16 == 0, "constant buffer size");

int32_t CGxDeviceD3d11::ICreateFixedFunction() {
    D3d9ShaderInfo vsInfo;
    vsInfo.m_isPixel = false;
    vsInfo.m_inputs.push_back({ 0, 0, 0 });    // POSITION
    vsInfo.m_inputs.push_back({ 3, 0, 1 });    // NORMAL
    vsInfo.m_inputs.push_back({ 10, 0, 2 });   // COLOR0
    vsInfo.m_inputs.push_back({ 10, 1, 3 });   // COLOR1

    for (uint32_t i = 0; i < 8; i++) {
        vsInfo.m_inputs.push_back({ 5, i, 4 + i });  // TEXCOORDi
    }

    D3d9ShaderInfo psInfo;
    psInfo.m_isPixel = true;

    std::string vsSource = kFixedFunctionVs;

    if (GetEnvironmentVariableA("WOW_D3D11_FFDIRECT", nullptr, 0)) {
        for (int i = 0; i < 8; i++) {
            char from[32];
            char to[96];
            snprintf(from, sizeof(from), "o.tc%d = outTc[%d];", i, i);
            char mode[8] = {};
            GetEnvironmentVariableA("WOW_D3D11_FFDIRECT", mode, sizeof(mode));

            if (mode[0] == '3') {
                snprintf(to, sizeof(to), "o.tc%d = float4(v.pos.x / 1024.0, (600.0 - v.pos.y) / 1024.0, 0, 1);", i);
            } else if (mode[0] == '2') {
                snprintf(to, sizeof(to), "o.tc%d = float4(%s, 0, 1);", i, i == 0 ? "0.5, 0.25" : "0.75, 0.125");
            } else {
                snprintf(to, sizeof(to), "o.tc%d = v.tc%d;", i, i);
            }

            size_t pos = vsSource.find(from);

            if (pos != std::string::npos) {
                vsSource.replace(pos, strlen(from), to);
            }
        }
    }

    {
        char mode[8] = {};

        if (GetEnvironmentVariableA("WOW_D3D11_FFDIRECT", mode, sizeof(mode)) && mode[0] == '4') {
            size_t pos = vsSource.find("o.col0 = col;");

            if (pos != std::string::npos) {
                vsSource.replace(pos, 13, "o.col0 = float4(1.0, 0.0, 0.0, 1.0);");
            }
        }
    }

    this->m_ff.m_vs = this->ICompileShader(vsSource, "vs_5_0", vsInfo, "FixedFunctionVS");
    this->m_ff.m_ps = this->ICompileShader(kFixedFunctionPs, "ps_5_0", psInfo, "FixedFunctionPS");
    this->m_ff.m_blitVs = this->ICompileShader(kBlitVs, "vs_5_0", D3d9ShaderInfo(), "BlitVS");
    this->m_ff.m_blitPs = this->ICompileShader(kBlitPs, "ps_5_0", psInfo, "BlitPS");

    this->m_ff.m_vsConstants = this->ICreateBuffer(sizeof(FFVSConstants), D3D11_BIND_CONSTANT_BUFFER);
    this->m_ff.m_psConstants = this->ICreateBuffer(sizeof(FFPSConstants), D3D11_BIND_CONSTANT_BUFFER);
    this->m_ff.m_blitConstants = this->ICreateBuffer(16, D3D11_BIND_CONSTANT_BUFFER);

    if (!this->m_ff.m_vs || !this->m_ff.m_ps || !this->m_ff.m_blitVs || !this->m_ff.m_blitPs || !this->m_ff.m_vsConstants || !this->m_ff.m_psConstants || !this->m_ff.m_blitConstants) {
        return 0;
    }

    // constant buffer slots never change: b0 = shadow constants, b1 = emulated state, b2 = fixed function, b3 = blit
    ID3D11Buffer* vsBuffers[4] = { this->m_constants[1], this->m_extraConstants[1], this->m_ff.m_vsConstants, this->m_ff.m_blitConstants };
    ID3D11Buffer* psBuffers[4] = { this->m_constants[0], this->m_extraConstants[0], this->m_ff.m_psConstants, this->m_ff.m_blitConstants };
    this->m_d3dContext->VSSetConstantBuffers(0, 4, vsBuffers);
    this->m_d3dContext->PSSetConstantBuffers(0, 4, psBuffers);
    return 1;
}

void CGxDeviceD3d11::IDestroyFixedFunction() {
    for (ShaderData** data : { &this->m_ff.m_vs, &this->m_ff.m_ps, &this->m_ff.m_blitVs, &this->m_ff.m_blitPs }) {
        if (*data) {
            Rel((*data)->m_shader);
            Rel((*data)->m_blob);
            delete *data;
            *data = nullptr;
        }
    }

    Rel(this->m_ff.m_vsConstants);
    Rel(this->m_ff.m_psConstants);
    Rel(this->m_ff.m_blitConstants);
}

static void CopyMatrix(float* dest, const C44Matrix& matrix) {
    memcpy(dest, &matrix, 16 * sizeof(float));
}

static void SetColor(float* dest, uint32_t color) {
    // 0xAARRGGBB
    dest[0] = ((color >> 16) & 0xFF) / 255.0f;
    dest[1] = ((color >> 8) & 0xFF) / 255.0f;
    dest[2] = (color & 0xFF) / 255.0f;
    dest[3] = ((color >> 24) & 0xFF) / 255.0f;
}

void CGxDeviceD3d11::IUpdateFixedFunctionConstants() {
    FFVSConstants vs;
    FFPSConstants ps;
    memset(&vs, 0, sizeof(vs));
    memset(&ps, 0, sizeof(ps));

    CGxMatrixStack& world = this->m_xforms[GxXform_World];
    CGxMatrixStack& view = this->m_xforms[GxXform_View];
    CopyMatrix(vs.world, world.m_mtx[world.m_level]);
    CopyMatrix(vs.view, view.m_mtx[view.m_level]);
    CopyMatrix(vs.proj, this->m_projNative);

    // texture coordinate generation and transforms (see IXformSetTex / ISetTexGen in the D3D9 backend)
    for (uint32_t i = 0; i < 8; i++) {
        int32_t texGen = this->m_appRenderStates[GxRs_TexGen0 + i].m_value.m_data.i[0];
        uint32_t coordSet = this->m_appRenderStates[GxRs_TextureCoord0 + i].m_value.m_data.u[0];
        int32_t shaderMode = this->m_appRenderStates[GxRs_TextureShader0 + i].m_value.m_data.i[0];

        float mode = 0.0f;

        switch (texGen) {
            case GxTexGen_ObjectLinear:
            case GxTexGen_EyeLinear:
            case GxTexGen_Unk3:
                mode = 1.0f;
                break;

            case GxTexGen_ReflectionMap:
            case GxTexGen_SphereMap:
                mode = 2.0f;
                break;

            case GxTexGen_Normal:
                mode = 3.0f;
                break;

            default:
                break;
        }

        CGxMatrixStack& texGenStack = this->m_texGen[i];
        CGxMatrixStack& xform = this->m_xforms[i];
        C44Matrix matrix = texGenStack.m_mtx[texGenStack.m_level];
        float dims = 0.0f;
        float projected = 0.0f;

        switch (shaderMode) {
            case 0:
                dims = (texGenStack.m_flags[texGenStack.m_level] & CGxMatrixStack::F_Identity) ? 0.0f : 3.0f;
                break;

            case 1: {
                C44Matrix product = texGenStack.m_mtx[texGenStack.m_level] * xform.m_mtx[xform.m_level];
                matrix = product;

                if (texGen) {
                    dims = 3.0f;
                } else {
                    dims = 2.0f;
                    matrix.c0 = product.d0;
                    matrix.c1 = product.d1;
                }

                break;
            }

            case 2:
                matrix = texGenStack.m_mtx[texGenStack.m_level] * xform.m_mtx[xform.m_level];
                dims = 3.0f;
                projected = 1.0f;
                break;

            default:
                break;
        }

        CopyMatrix(vs.texMat[i], matrix);
        vs.texCtl[i][0] = mode;
        vs.texCtl[i][1] = static_cast<float>(coordSet & 7);
        vs.texCtl[i][2] = dims;
        vs.texCtl[i][3] = projected;
    }

    // lighting and materials (see IStateSyncLights / IStateSyncMaterial)
    bool hasColor = (this->m_primVertexMask & (1 << GxVA_Color0)) != 0;
    uint32_t diffuse = this->m_appRenderStates[GxRs_MatDiffuse].m_value.m_data.u[0];
    uint32_t emissive = this->m_appRenderStates[GxRs_MatEmissive].m_value.m_data.u[0];
    bool lighting = false;
    float ambientLevel = 0.0f;
    bool lightsOn = false;

    if (this->m_appRenderStates[GxRs_Lighting].m_value.m_data.i[0]) {
        lighting = (this->m_appMasterEnables & 0x1) != 0;
        lightsOn = true;
    } else if (!hasColor && diffuse != 0xFFFFFFFF) {
        // "ambient only" mode: lit with a white ambient term and no lights
        lighting = true;
        ambientLevel = 1.0f;
    }

    SetColor(vs.matDiffuse, diffuse);
    SetColor(vs.matAmbient, diffuse);
    SetColor(vs.matEmissive, emissive);
    vs.globalAmbient[0] = vs.globalAmbient[1] = vs.globalAmbient[2] = ambientLevel;
    vs.globalAmbient[3] = 1.0f;

    int32_t colorMaterial = this->m_appRenderStates[GxRs_ColorMaterial].m_value.m_data.i[0];
    vs.flags[0] = lighting ? 1.0f : 0.0f;
    vs.flags[2] = hasColor ? 1.0f : 0.0f;
    vs.flags2[0] = hasColor && colorMaterial == 0 ? 1.0f : 0.0f;
    vs.flags2[1] = hasColor && colorMaterial == 0 ? 1.0f : 0.0f;
    vs.flags2[3] = hasColor && colorMaterial == 2 ? 1.0f : 0.0f;

    for (uint32_t i = 0; i < 4; i++) {
        const CGxApiLight& light = this->m_lights[i];

        if (!lightsOn || !light.m_enable) {
            continue;
        }

        vs.lightPos[i][0] = light.m_dir.x;
        vs.lightPos[i][1] = light.m_dir.y;
        vs.lightPos[i][2] = light.m_dir.z;
        vs.lightPos[i][3] = light.m_dir.w == 1.0f ? 1.0f : 0.0f;
        vs.lightDiffuse[i][0] = light.m_dirColor.x;
        vs.lightDiffuse[i][1] = light.m_dirColor.y;
        vs.lightDiffuse[i][2] = light.m_dirColor.z;
        vs.lightAmbient[i][0] = light.m_ambColor.x;
        vs.lightAmbient[i][1] = light.m_ambColor.y;
        vs.lightAmbient[i][2] = light.m_ambColor.z;
        vs.lightAtten[i][0] = light.m_constantAttenuation;
        vs.lightAtten[i][1] = light.m_linearAttenuation;
        vs.lightAtten[i][2] = light.m_quadraticAttenuation;
        vs.lightAtten[i][3] = 1.0f;
    }

    // fog
    uint32_t fogColor = this->m_appRenderStates[GxRs_FogColor].m_value.m_data.u[0];
    bool fog = (this->m_appMasterEnables & 0x2) && this->m_appRenderStates[GxRs_Fog].m_value.m_data.i[0];
    vs.flags[1] = fog ? 1.0f : 0.0f;
    vs.fog[0] = this->m_appRenderStates[GxRs_FogStart].m_value.m_data.f[0];
    vs.fog[1] = this->m_appRenderStates[GxRs_FogEnd].m_value.m_data.f[0];

    if (vs.fog[1] == vs.fog[0]) {
        vs.fog[1] = vs.fog[0] + 1.0f;
    }

    // user clip planes are world space for the fixed function path
    uint32_t planeMask = this->m_appRenderStates[GxRs_ClipPlaneMask].m_value.m_data.u[0];

    for (uint32_t i = 0; i < 6; i++) {
        if (planeMask & (1u << i)) {
            vs.clipPlane[i][0] = this->m_clipPlane[i].n.x;
            vs.clipPlane[i][1] = this->m_clipPlane[i].n.y;
            vs.clipPlane[i][2] = this->m_clipPlane[i].n.z;
            vs.clipPlane[i][3] = this->m_clipPlane[i].d;
        }
    }

    // pixel stage: a stage is active when its texture is bound; the first unbound stage ends the cascade
    for (uint32_t s = 0; s < 8; s++) {
        CGxTex* texId = static_cast<CGxTex*>(this->m_appRenderStates[GxRs_Texture0 + s].m_value.m_data.p);

        if (!texId) {
            break;
        }

        ps.stage[s][0] = this->m_appRenderStates[GxRs_ColorOp0 + s].m_value.m_data.u[0] % 6 + 1;
        ps.stage[s][1] = this->m_appRenderStates[GxRs_AlphaOp0 + s].m_value.m_data.u[0] % 6 + 1;
    }

    SetColor(ps.fogColor, fogColor);
    ps.fogColor[3] = fog ? 1.0f : 0.0f;

    int32_t alphaRef = this->m_appRenderStates[GxRs_AlphaRef].m_value.m_data.i[0];
    ps.alphaTest[0] = alphaRef > 0 ? 1.0f : 0.0f;
    ps.alphaTest[1] = alphaRef / 255.0f;

    static int32_t s_ffConstLogs = 0;

    if (s_ffConstLogs++ < 8) {
        D3d11Log("FF constants: world d %g %g %g %g | view d %g %g %g %g | proj row0 %g %g %g %g row1 %g %g %g %g row3 %g %g %g %g | lighting %g hasColor %g | texCtl0 %g %g %g %g texCtl1 %g %g %g %g | texMat0 row0 %g %g %g %g row1 %g %g %g %g row3 %g %g %g %g", vs.world[0], vs.world[5], vs.world[10], vs.world[15], vs.view[0], vs.view[5], vs.view[10], vs.view[15], vs.proj[0], vs.proj[1], vs.proj[2], vs.proj[3], vs.proj[4], vs.proj[5], vs.proj[6], vs.proj[7], vs.proj[12], vs.proj[13], vs.proj[14], vs.proj[15], vs.flags[0], vs.flags[2], vs.texCtl[0][0], vs.texCtl[0][1], vs.texCtl[0][2], vs.texCtl[0][3], vs.texCtl[1][0], vs.texCtl[1][1], vs.texCtl[1][2], vs.texCtl[1][3], vs.texMat[0][0], vs.texMat[0][1], vs.texMat[0][2], vs.texMat[0][3], vs.texMat[0][4], vs.texMat[0][5], vs.texMat[0][6], vs.texMat[0][7], vs.texMat[0][12], vs.texMat[0][13], vs.texMat[0][14], vs.texMat[0][15]);
    }

    this->m_d3dContext->UpdateSubresource(this->m_ff.m_vsConstants, 0, nullptr, &vs, 0, 0);
    this->m_d3dContext->UpdateSubresource(this->m_ff.m_psConstants, 0, nullptr, &ps, 0, 0);
}

// ---------------------------------------------------------------------------------------------
// StretchRect style copy: draws a screen sized triangle sampling `srcSrv` into one mip / face of `dst`

int32_t CGxDeviceD3d11::IBlit(ID3D11ShaderResourceView* srcSrv, uint32_t srcWidth, uint32_t srcHeight, const RECT* srcRect, TexData* dst, uint32_t face, uint32_t level, const RECT* dstRect, bool linear) {
    if (!srcSrv || !dst || !dst->m_texture || !this->m_ff.m_blitVs) {
        return 0;
    }

    ID3D11RenderTargetView* rtv = nullptr;
    bool ownRtv = false;

    if (level == 0 && face < 6 && dst->m_rtv[face]) {
        rtv = dst->m_rtv[face];
    } else {
        D3D11_TEXTURE2D_DESC desc;
        dst->m_texture->GetDesc(&desc);

        if (!(desc.BindFlags & D3D11_BIND_RENDER_TARGET)) {
            return 0;
        }

        D3D11_RENDER_TARGET_VIEW_DESC rd = {};
        rd.Format = desc.Format;

        if (desc.ArraySize > 1) {
            rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            rd.Texture2DArray.MipSlice = level;
            rd.Texture2DArray.FirstArraySlice = face;
            rd.Texture2DArray.ArraySize = 1;
        } else {
            rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            rd.Texture2D.MipSlice = level;
        }

        if (FAILED(this->m_d3dDevice->CreateRenderTargetView(dst->m_texture, &rd, &rtv))) {
            return 0;
        }

        ownRtv = true;
    }

    uint32_t mipWidth = dst->m_width >> level;
    uint32_t mipHeight = dst->m_height >> level;

    if (!mipWidth) {
        mipWidth = 1;
    }

    if (!mipHeight) {
        mipHeight = 1;
    }

    RECT fullDst = { 0, 0, static_cast<LONG>(mipWidth), static_cast<LONG>(mipHeight) };
    RECT fullSrc = { 0, 0, static_cast<LONG>(srcWidth), static_cast<LONG>(srcHeight) };
    const RECT& d = dstRect && dstRect->right > dstRect->left ? *dstRect : fullDst;
    const RECT& s = srcRect && srcRect->right > srcRect->left ? *srcRect : fullSrc;

    float rect[4] = { static_cast<float>(s.left) / srcWidth, static_cast<float>(s.top) / srcHeight, static_cast<float>(s.right) / srcWidth, static_cast<float>(s.bottom) / srcHeight };
    this->m_d3dContext->UpdateSubresource(this->m_ff.m_blitConstants, 0, nullptr, rect, 0, 0);

    ID3D11SamplerState* sampler = this->IGetSamplerState(linear ? GxTex_Linear : GxTex_Nearest, 1, 0, 0);

    D3D11_VIEWPORT viewport = { static_cast<float>(d.left), static_cast<float>(d.top), static_cast<float>(d.right - d.left), static_cast<float>(d.bottom - d.top), 0.0f, 1.0f };

    // unbind anything that aliases the destination, then draw
    ID3D11ShaderResourceView* nullSrv = nullptr;
    this->m_d3dContext->PSSetShaderResources(0, 1, &nullSrv);

    this->m_d3dContext->OMSetRenderTargets(1, &rtv, nullptr);
    this->m_d3dContext->RSSetViewports(1, &viewport);

    static const float blendFactor[4] = { 1, 1, 1, 1 };
    this->m_d3dContext->OMSetBlendState(nullptr, blendFactor, 0xFFFFFFFF);
    this->m_d3dContext->OMSetDepthStencilState(nullptr, 0);
    this->m_d3dContext->RSSetState(nullptr);
    this->m_d3dContext->IASetInputLayout(nullptr);
    this->m_d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    this->m_d3dContext->VSSetShader(static_cast<ID3D11VertexShader*>(this->m_ff.m_blitVs->m_shader), nullptr, 0);
    this->m_d3dContext->PSSetShader(static_cast<ID3D11PixelShader*>(this->m_ff.m_blitPs->m_shader), nullptr, 0);
    this->m_d3dContext->PSSetShaderResources(0, 1, &srcSrv);
    this->m_d3dContext->PSSetSamplers(0, 1, &sampler);
    this->m_d3dContext->Draw(3, 0);
    this->m_d3dContext->PSSetShaderResources(0, 1, &nullSrv);

    if (ownRtv) {
        rtv->Release();
    }

    // everything the draw path caches has been replaced
    this->m_curBlend = nullptr;
    this->m_curDepth = nullptr;
    this->m_curRaster = nullptr;
    this->m_curLayout = nullptr;
    this->m_curVs = nullptr;
    this->m_curPs = nullptr;
    this->m_curTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    memset(this->m_curVertexBuffers, 0, sizeof(this->m_curVertexBuffers));
    memset(this->m_curSrvs, 0, sizeof(this->m_curSrvs));
    memset(this->m_curSamplers, 0, sizeof(this->m_curSamplers));
    this->m_targetsDirty = 1;
    this->m_viewportDirty = 1;
    this->m_scissorRectDirty = 1;
    return 1;
}
