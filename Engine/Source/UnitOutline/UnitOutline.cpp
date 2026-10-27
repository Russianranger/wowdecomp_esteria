// Not in the original 3.3.5a binary. See UnitOutline.h.
//
// Derived from manatactical/wooooow's UnitOutline (itself after WarcraftXL's wxl-mana-unit-outline). The target
// bookkeeping and the stamp-the-model's-own-batches idea are theirs; the masks, the edge pass and the Vulkan
// program are this tree's.
#include "Engine/Source/UnitOutline/UnitOutline.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <d3dcompiler.h>

#include "Engine/Source/Base/Handle.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include "Engine/Source/FFX/FFX.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxDevice/CGxShader.h"
#include "Engine/Source/Gx/CGxTex.h"
#include "Engine/Source/Gx/CGxTexFlags.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/Services/Texture.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/Tempest/C2Vector.h"
#include "Engine/Source/Tempest/C3Vector.h"
#include "Engine/Source/Tempest/CRect.h"
#include "WoW/Source/Ui/OptionCVars.h"

static_assert(GxBlend_Opaque == 0 && GxBlend_Alpha == 2, "the stamp compares blend modes by number");

namespace {

constexpr int32_t kMaxTargets = 64;      // selections and quest givers, each up to two models plus gear
constexpr int32_t kMaxGroups = 4;        // distinct outline colours drawn in one frame, each with its own mask
constexpr int32_t kMaxPlayerModels = 16;

// The settle fade a selection plays after it appears: full strength at first, eased to kFadeEndOpacity over
// kFadeMs. A unit marked keepFull (a quest giver) stays at full strength.
constexpr uint32_t kFadeMs = 800;
constexpr float kFadeEndOpacity = 0.4f;

struct Target {
    CM2Model* m_model;
    int32_t m_group;
    bool m_keepFull;
    uint32_t m_birth;
};

struct Group {
    float m_rgb[3];
    CTexture* m_texture;
    bool m_cleared;       // the group's mask was stamped (so cleared) this frame
};

struct PrevTarget {
    CM2Model* m_model;
    uint32_t m_birth;
};

CVar* s_cvarUnitOutline;
CVar* s_cvarWidth;
CVar* s_cvarOpacity;
CVar* s_cvarThroughWalls;
CVar* s_cvarSettleFade;
CVar* s_cvarSettleOpacity;

Target s_targets[kMaxTargets];
int32_t s_count;
PrevTarget s_prev[kMaxTargets];
int32_t s_prevCount;
Group s_groups[kMaxGroups];
int32_t s_groupCount;

CM2Model* s_playerModels[kMaxPlayerModels];
int32_t s_playerCount;
CTexture* s_playerTexture;
bool s_playerCleared;

C2iVector s_size;               // the size every mask has; zero until they exist
bool s_depthShared;             // the stamp can be depth tested against the scene (equal-sized targets share a depth buffer)
bool s_inComposite;
CGxShader* s_edgeShader;
bool s_edgeFailed;

// The edge pass for the backends that compile it: a D3D9 pixel shader (D3D11 translates the same bytecode).
// Vulkan has the pass built in, under the same name (Shaders/OutlineEdge.frag).
//   s0 = a group's mask (R = 1 - silhouette, G = 1 - visible silhouette), s1 = the player's mask (R)
//   c0 = { 1 / mask width, 1 / mask height, line width in pixels, unused }
//   c1 = { line colour rgb, strength }
const char* const kEdgeHLSL =
    "sampler2D s0 : register(s0);\n"
    "sampler2D s1 : register(s1);\n"
    "float4 c0 : register(c0);\n"
    "float4 c1 : register(c1);\n"
    "float4 main(float2 uv : TEXCOORD0) : COLOR0 {\n"
    "  float inside = 1.0 - tex2D(s0, uv).r;\n"
    "  float player = 1.0 - tex2D(s1, uv).r;\n"
    "  float cover = 1.0 - tex2D(s0, uv).g;\n"
    "  float2 texel = c0.xy;\n"
    "  float radius = max(c0.z, 0.5);\n"
    "  [unroll] for (int i = 0; i < 12; i++) {\n"
    "    float a = 6.2831853 * i / 12.0;\n"
    "    float2 d = float2(cos(a), sin(a)) * texel;\n"
    "    cover = max(cover, 1.0 - tex2D(s0, uv + d * radius * 0.5).g);\n"
    "    cover = max(cover, 1.0 - tex2D(s0, uv + d * radius * 0.8).g);\n"
    "    cover = max(cover, (1.0 - tex2D(s0, uv + d * radius).g) * 0.7);\n"
    "  }\n"
    "  return float4(c1.rgb, cover * (1.0 - inside) * (1.0 - player) * c1.a);\n"
    "}\n";

bool IsVulkan() {
    return g_theGxDevicePtr->DeviceApi() == GxApi_Vulkan;
}

CGxTex* GxTexOf(CTexture* texture) {
    return texture ? TextureGetGxTex(texture, 1, nullptr) : nullptr;
}

// A frame-sized render target, exactly the size of the window so it pairs with the scene's depth where the
// backend pairs by size. Not rounded to a power of two for the same reason.
bool CreateMask(CTexture*& texture, const C2iVector& size) {
    EGxTexFormat format;

    if (g_theGxDevicePtr->Caps().m_rttFormat[GxTex_Argb8888]) {
        format = GxTex_Argb8888;
    } else if (g_theGxDevicePtr->Caps().m_rttFormat[GxTex_Abgr8888]) {
        format = GxTex_Abgr8888;
    } else {
        return false;
    }

    if (g_theGxDevicePtr->Caps().m_texTarget[GxTex_NonPow2] != 1) {
        return false;
    }

    int32_t maxSize = static_cast<int32_t>(g_theGxDevicePtr->Caps().m_texMaxSize[GxTex_NonPow2]);

    if (size.x < 8 || size.y < 8 || size.x > maxSize || size.y > maxSize) {
        return false;
    }

    CGxTexFlags flags(GxTex_Linear, 0, 0, 0, 0, 1, 1, 0, 0, 0);
    texture = TextureCreate(GxTex_NonPow2, size.x, size.y, 0, format, format, flags, nullptr, GxTexNullUserFunc, "UnitOutline::Mask", 0);
    return texture && GxTexOf(texture);
}

void DestroyMask(CTexture*& texture) {
    if (texture) {
        HandleClose(reinterpret_cast<HOBJECT>(texture));
        texture = nullptr;
    }
}

void DestroyMasks() {
    for (int32_t i = 0; i < kMaxGroups; i++) {
        DestroyMask(s_groups[i].m_texture);
    }

    DestroyMask(s_playerTexture);
    s_size.x = 0;
    s_size.y = 0;
}

// Makes the edge pass's shader object once. Vulkan finds its built-in program by the name; D3D9 and D3D11 take the
// compiled bytecode.
bool EnsureEdgeShader() {
    if (s_edgeShader) {
        return true;
    }

    if (s_edgeFailed) {
        return false;
    }

    s_edgeFailed = true;
    EGxApi api = g_theGxDevicePtr->DeviceApi();
    ID3DBlob* code = nullptr;

    if (!IsVulkan()) {
        if (api != GxApi_D3d9 && api != GxApi_D3d9Ex && api != GxApi_D3d11) {
            return false;
        }

        ID3DBlob* error = nullptr;
        HRESULT result = D3DCompile(kEdgeHLSL, strlen(kEdgeHLSL), "UnitOutline", nullptr, nullptr, "main", "ps_3_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &error);

        if (error) {
            error->Release();
        }

        if (FAILED(result) || !code) {
            if (code) {
                code->Release();
            }

            return false;
        }
    }

    CGxShader* shader = new (__FILE__, __LINE__) CGxShader();
    shader->m_target = GxSh_Pixel;
    shader->m_refCount = 1;
    shader->m_key = "UnitOutlineEdge";

    if (code) {
        shader->m_code.SetCount(static_cast<uint32_t>(code->GetBufferSize()));
        memcpy(shader->m_code.m_data, code->GetBufferPointer(), code->GetBufferSize());
        code->Release();
    }

    if (!shader->Valid()) {
        return false;
    }

    s_edgeShader = shader;
    s_edgeFailed = false;
    return true;
}

}  // namespace

void CUnitOutline::RegisterCVars() {
    s_cvarUnitOutline = OptionCVars::Register("unitOutline", "Outline the target and mouseover units", "1");
    s_cvarWidth = OptionCVars::Register("unitOutlineWidth", "Unit outline width in pixels", "2");
    s_cvarOpacity = OptionCVars::Register("unitOutlineOpacity", "Unit outline opacity", "1");
    s_cvarThroughWalls = OptionCVars::Register("unitOutlineThroughWalls", "Show unit outlines through terrain and buildings", "0");
    s_cvarSettleFade = OptionCVars::Register("unitOutlineSettleFade", "Dim a unit's outline after it has been selected for a moment", "1");
    s_cvarSettleOpacity = OptionCVars::Register("unitOutlineSettleOpacity", "Opacity a settled unit outline dims to", "0.4");
}

bool CUnitOutline::Enabled() {
    return s_cvarUnitOutline && s_cvarUnitOutline->m_intValue && g_theGxDevicePtr && g_theGxDevicePtr->m_context;
}

void CUnitOutline::BeginTargets() {
    s_count = 0;
    s_groupCount = 0;
    s_playerCount = 0;
}

void CUnitOutline::SetPlayerModel(CM2Model* model) {
    SetPlayerModel(model, nullptr);
}

void CUnitOutline::SetPlayerModel(CM2Model* model, CM2Model* mount) {
    s_playerCount = 0;

    if (model) {
        s_playerModels[s_playerCount++] = model;
    }

    if (mount && s_playerCount < kMaxPlayerModels) {
        s_playerModels[s_playerCount++] = mount;
    }
}

void CUnitOutline::AddOccluder(CM2Model* model) {
    if (!model || s_playerCount >= kMaxPlayerModels) {
        return;
    }

    for (int32_t i = 0; i < s_playerCount; i++) {
        if (s_playerModels[i] == model) {
            return;
        }
    }

    s_playerModels[s_playerCount++] = model;
}

int32_t CUnitOutline::FindGroup(const float rgb[3], bool create) {
    for (int32_t i = 0; i < s_groupCount; i++) {
        if (fabsf(s_groups[i].m_rgb[0] - rgb[0]) < 0.004f && fabsf(s_groups[i].m_rgb[1] - rgb[1]) < 0.004f && fabsf(s_groups[i].m_rgb[2] - rgb[2]) < 0.004f) {
            return i;
        }
    }

    if (!create || s_groupCount >= kMaxGroups) {
        return -1;
    }

    Group& group = s_groups[s_groupCount];
    memcpy(group.m_rgb, rgb, sizeof(group.m_rgb));
    group.m_cleared = false;
    return s_groupCount++;
}

void CUnitOutline::AddEntry(CM2Model* model, const float rgb[3], bool keepFull) {
    if (!model || s_count >= kMaxTargets) {
        return;
    }

    for (int32_t i = 0; i < s_count; i++) {
        if (s_targets[i].m_model == model) {
            // A model can arrive twice (a quest giver that is also the target); staying visible wins, the
            // colour stays as the first caller chose.
            s_targets[i].m_keepFull = s_targets[i].m_keepFull || keepFull;
            return;
        }
    }

    int32_t group = FindGroup(rgb, true);

    if (group < 0) {
        return;
    }

    Target& target = s_targets[s_count];
    target.m_model = model;
    target.m_group = group;
    target.m_keepFull = keepFull;
    target.m_birth = OsGetAsyncTimeMs();

    // The list is rebuilt every frame, so a model already seen last frame keeps its birth or the fade would
    // replay forever.
    for (int32_t i = 0; i < s_prevCount; i++) {
        if (s_prev[i].m_model == model) {
            target.m_birth = s_prev[i].m_birth;
            break;
        }
    }

    s_count++;
}

void CUnitOutline::AddTarget(CM2Model* model, const float rgb[3], bool keepFull, CM2Model* footRef) {
    (void)footRef;
    AddEntry(model, rgb, keepFull);
}

void CUnitOutline::AddDoodad(CM2Model* model, const float rgb[3]) {
    AddEntry(model, rgb, false);
}

int32_t CUnitOutline::FindTarget(CM2Model* model) {
    for (int32_t i = 0; i < s_count; i++) {
        if (s_targets[i].m_model == model) {
            return i;
        }
    }

    return -1;
}

// The gather fills the list whether or not the option is on, and this answer reorders the model pass, so it
// has to be gated on the option or a player who never turned the outline on would draw their world in a
// different order.
bool CUnitOutline::IsTarget(CM2Model* model) {
    if (!model || s_inComposite || !s_count || !Enabled()) {
        return false;
    }

    return FindTarget(model) >= 0;
}

bool CUnitOutline::HasTargets() {
    return !s_inComposite && s_count > 0 && Enabled();
}

bool CUnitOutline::EnsureResources() {
    if (!Enabled() || !EnsureEdgeShader()) {
        return false;
    }

    if (FFX::s_useRECT) {
        return false;
    }

    CRect window = { 0.0f, 0.0f, 0.0f, 0.0f };
    g_theGxDevicePtr->CapsWindowSize(window);
    C2iVector size = { static_cast<int32_t>(window.maxX - window.minX), static_cast<int32_t>(window.maxY - window.minY) };

    if (size.x <= 0 || size.y <= 0) {
        return false;
    }

    if (size.x != s_size.x || size.y != s_size.y) {
        DestroyMasks();

        if (!CreateMask(s_playerTexture, size)) {
            DestroyMasks();
            return false;
        }

        s_size = size;
    }

    // Groups get their masks on first use, so a frame with one colour costs one mask.
    for (int32_t i = 0; i < s_groupCount; i++) {
        if (!s_groups[i].m_texture && !CreateMask(s_groups[i].m_texture, s_size)) {
            return false;
        }
    }

    // Targets of the same size share a depth buffer where the backend pairs by size (Vulkan), which is what
    // lets the stamp be tested against the scene. Elsewhere both channels hold the same silhouette.
    s_depthShared = IsVulkan();
    return true;
}

// Draws one of the model's own batches into a mask channel, the batch's shaders untouched: the mask is cleared
// white and the batch erases it with inverse source alpha, so wherever the batch draws a pixel the channel goes
// to zero. No pixel shader has to cooperate, and an alpha-tested batch follows its own cutout.
void CUnitOutline::Stamp(const CGxBatch& batch, void* texture, bool& cleared, uint32_t channel, bool depthTest) {
    CGxTex* target = GxTexOf(static_cast<CTexture*>(texture));

    if (!target) {
        return;
    }

    // The blend state says what the batch is: opaque, alpha-keyed and alpha-blended batches are the body, hair
    // and cloth; additive effects, attachments and glows are left out, or their quads would be outlined too.
    int32_t blend = g_theGxDevicePtr->m_appRenderStates[GxRs_BlendingMode].m_value.m_data.i[0];

    if (blend != GxBlend_Opaque && blend != GxBlend_AlphaKey && blend != GxBlend_Alpha) {
        return;
    }

    float oldMinX;
    float oldMaxX;
    float oldMinY;
    float oldMaxY;
    float oldMinZ;
    float oldMaxZ;
    GxXformViewport(oldMinX, oldMaxX, oldMinY, oldMaxY, oldMinZ, oldMaxZ);

    // The render target is not part of the render state stack, so the caller's own is put back by hand.
    CGxTex* oldTarget = nullptr;
    g_theGxDevicePtr->DeviceGetRenderTarget(GxBuffers_Color, oldTarget);

    g_theGxDevicePtr->RsPush();
    GxRenderTargetSet(GxBuffers_Color, target, 0);
    // The mask is sampled in whole-target pixel space, so x and y are 1:1; the z range is the world's, or
    // the stamp would store every unit deeper than it is and be occluded by geometry at its own distance.
    GxXformSetViewport(0.0f, 1.0f, 0.0f, 1.0f, oldMinZ, oldMaxZ);

    if (!cleared) {
        GxSceneClear(GxBufferBit_Color, CImVector(0xFF, 0xFF, 0xFF, 0xFF));
        cleared = true;
    }

    GxRsSet(GxRs_BlendingMode, GxBlend_InvSrcAlphaOpaque);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, 0);
    GxRsSet(GxRs_DepthTest, depthTest && s_depthShared ? 1 : 0);
    GxRsSet(GxRs_DepthFunc, 0);
    GxRsSet(GxRs_DepthWrite, 0);
    GxRsSet(GxRs_ColorWrite, static_cast<int32_t>(channel));
    GxDraw(const_cast<CGxBatch*>(&batch), 1);

    g_theGxDevicePtr->RsPop();
    GxRenderTargetSet(GxBuffers_Color, oldTarget, 0);
    GxXformSetViewport(oldMinX, oldMaxX, oldMinY, oldMaxY, oldMinZ, oldMaxZ);
}

void CUnitOutline::StampBatch(const CGxBatch& batch, CM2Model* model) {
    // While the option is off the module is a pure pass-through: nothing is created and no batch is drawn twice.
    if (!s_count && !s_playerCount) {
        return;
    }

    if (s_inComposite || !model || !Enabled() || !EnsureResources()) {
        return;
    }

    int32_t index = FindTarget(model);

    if (index >= 0) {
        Group& group = s_groups[s_targets[index].m_group];
        // R: the whole silhouette, which gives the line its shape; G: what the depth test lets through, which
        // gives it its visibility. Untested first so a wall's rim is never the line's shape.
        Stamp(batch, group.m_texture, group.m_cleared, 1, false);
        Stamp(batch, group.m_texture, group.m_cleared, 2, !OptionCVars::Bool(s_cvarThroughWalls, false));
        return;
    }

    for (int32_t i = 0; i < s_playerCount; i++) {
        if (model == s_playerModels[i]) {
            Stamp(batch, s_playerTexture, s_playerCleared, 1, false);
            return;
        }
    }
}

void CUnitOutline::EdgePass(int32_t groupIndex) {
    Group& group = s_groups[groupIndex];

    // The settle fade: the strongest of the group's targets.
    float weight = 0.0f;
    uint32_t now = OsGetAsyncTimeMs();

    for (int32_t i = 0; i < s_count; i++) {
        if (s_targets[i].m_group != groupIndex) {
            continue;
        }

        float w = 1.0f;

        if (!s_targets[i].m_keepFull && OptionCVars::Bool(s_cvarSettleFade, true)) {
            uint32_t age = now - s_targets[i].m_birth;
            float t = age >= kFadeMs ? 1.0f : static_cast<float>(age) / static_cast<float>(kFadeMs);
            w = 1.0f - (1.0f - OptionCVars::Float(s_cvarSettleOpacity, 0.1f, 1.0f, kFadeEndOpacity)) * (t * t);
        }

        weight = std::max(weight, w);
    }

    weight *= OptionCVars::Float(s_cvarOpacity, 0.1f, 1.0f, 1.0f);

    float width = OptionCVars::Float(s_cvarWidth, 1.0f, 6.0f, 2.0f);

    auto channel = [](float v) { return static_cast<uint8_t>(std::min(std::max(v, 0.0f), 1.0f) * 255.0f + 0.5f); };
    CImVector color(channel(group.m_rgb[2]), channel(group.m_rgb[1]), channel(group.m_rgb[0]), channel(weight));

    C3Vector positions[4];
    C2Vector texcoords[4];
    C2Vector offsets[4] = { { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f } };
    C2Vector scale = { 0.0f, 0.0f };
    FFX::SetupGeo_Quad(s_size, s_size, s_size, positions, texcoords, false);
    FFX::SetupVB_PCT4(positions, color, texcoords, offsets, scale);

    // Vulkan reads the width from the alpha reference (in 1/16 pixel) and the colour from the vertices; the
    // compiled shaders read both from constants.
    const float pixelC0[4] = { 1.0f / static_cast<float>(s_size.x), 1.0f / static_cast<float>(s_size.y), width, 0.0f };
    const float pixelC1[4] = { group.m_rgb[0], group.m_rgb[1], group.m_rgb[2], weight };
    GxShaderConstantsSet(GxSh_Pixel, 0, pixelC0, 1);
    GxShaderConstantsSet(GxSh_Pixel, 1, pixelC1, 1);

    GxRsSet(GxRs_PixelShader, s_edgeShader);
    GxRsSet(GxRs_BlendingMode, GxBlend_Alpha);
    GxRsSet(GxRs_AlphaRef, static_cast<int32_t>(width * 16.0f + 0.5f));
    GxRsSet(GxRs_Texture0, GxTexOf(group.m_texture));
    GxRsSet(GxRs_Texture1, GxTexOf(s_playerTexture));

    g_theGxDevicePtr->PrimVertexMask(0x3D1);
    FFX::SetupIB();
    GxDraw(&FFX::s_quadBatch, 1);
}

// The end of the world draw, while the world's own render target is the one bound. The line goes in under the
// interface, so the HUD, nameplates and menus stay on top of it.
void CUnitOutline::Composite() {
    if (s_inComposite) {
        return;
    }

    // This frame's list becomes the reference for the next one's fade clocks, whatever happens below.
    s_prevCount = s_count;

    for (int32_t i = 0; i < s_count; i++) {
        s_prev[i].m_model = s_targets[i].m_model;
        s_prev[i].m_birth = s_targets[i].m_birth;
    }

    bool stamped = false;

    for (int32_t i = 0; i < s_groupCount; i++) {
        stamped = stamped || s_groups[i].m_cleared;
    }

    // A selected unit that was culled, off screen or hidden this frame stamped nothing, so its mask still holds
    // the last frame it was drawn in; compositing that would leave a line behind at the old position.
    if (!stamped || !EnsureResources()) {
        BeginTargets();
        s_playerCleared = false;
        return;
    }

    s_inComposite = true;

    CGxTex* frameTarget = nullptr;
    g_theGxDevicePtr->DeviceGetRenderTarget(GxBuffers_Color, frameTarget);

    // The player's mask has to be a real, white-cleared texture even when the player was not stamped.
    if (!s_playerCleared) {
        CGxTex* playerTarget = GxTexOf(s_playerTexture);

        if (playerTarget) {
            GxRenderTargetSet(GxBuffers_Color, playerTarget, 0);
            GxSceneClear(GxBufferBit_Color, CImVector(0xFF, 0xFF, 0xFF, 0xFF));
            GxRenderTargetSet(GxBuffers_Color, frameTarget, 0);
        }
    }

    // The engine's own full-screen setup: pixel-space ortho projection, identity view and world, a render
    // state push. It binds the texture it is given as the target, so the frame's own is put back right after.
    FFX::Tex setup = {};
    setup.m_size = s_size;
    setup.m_texSize = s_size;
    FFX::SetupGx(&setup);
    GxXformSetViewport(0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f);
    GxRenderTargetSet(GxBuffers_Color, frameTarget, 0);

    for (int32_t i = 0; i < s_groupCount; i++) {
        if (s_groups[i].m_cleared) {
            EdgePass(i);
        }
    }

    GxRsSet(GxRs_Texture0, nullptr);
    GxRsSet(GxRs_Texture1, nullptr);
    GxRsSet(GxRs_PixelShader, static_cast<void*>(nullptr));

    FFX::SetupReset();

    if (frameTarget) {
        // SetupReset puts the default back buffer back; the world was drawing somewhere else.
        GxRenderTargetSet(GxBuffers_Color, frameTarget, 0);
    }

    s_inComposite = false;
    s_playerCleared = false;
    BeginTargets();
}
