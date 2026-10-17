#include <cstdint>
#include <cstring>
#include "Engine/Source/Gx/CGxDeviceD3d11/CGxDeviceD3d11.h"
#include "Engine/Source/Gx/CGxBuf.h"
#include "Engine/Source/Gx/CGxPool.h"
#include "Engine/Source/Gx/CGxTex.h"
#include "Engine/Source/Gx/Gx.h"

template <class T>
static void Rel(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

// Gx blend modes -> D3D blend factors (same tables as the D3D9 backend)
static const D3D11_BLEND s_srcBlend[12] = { D3D11_BLEND_ONE, D3D11_BLEND_ONE, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_DEST_COLOR, D3D11_BLEND_DEST_COLOR, D3D11_BLEND_DEST_COLOR, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_ONE, D3D11_BLEND_BLEND_FACTOR };
static const D3D11_BLEND s_dstBlend[12] = { D3D11_BLEND_ZERO, D3D11_BLEND_ZERO, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_SRC_COLOR, D3D11_BLEND_ONE, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_ZERO, D3D11_BLEND_ONE, D3D11_BLEND_INV_BLEND_FACTOR };
static const D3D11_COMPARISON_FUNC s_cmpFunc[4] = { D3D11_COMPARISON_LESS_EQUAL, D3D11_COMPARISON_EQUAL, D3D11_COMPARISON_GREATER_EQUAL, D3D11_COMPARISON_LESS };

// the alpha channel cannot use colour factors
static D3D11_BLEND AlphaFactor(D3D11_BLEND factor) {
    switch (factor) {
        case D3D11_BLEND_SRC_COLOR:
            return D3D11_BLEND_SRC_ALPHA;

        case D3D11_BLEND_INV_SRC_COLOR:
            return D3D11_BLEND_INV_SRC_ALPHA;

        case D3D11_BLEND_DEST_COLOR:
            return D3D11_BLEND_DEST_ALPHA;

        case D3D11_BLEND_INV_DEST_COLOR:
            return D3D11_BLEND_INV_DEST_ALPHA;

        default:
            return factor;
    }
}

void CGxDeviceD3d11::IRsSendToHw(EGxRenderState which) {
    // Render state is read straight from m_appRenderStates when a draw is issued.
    // Only textures need a hook so they are uploaded as soon as they are bound.
    if (which >= GxRs_Texture0 && which <= GxRs_Texture15) {
        CGxTex* texId = static_cast<CGxTex*>(this->m_appRenderStates[which].m_value.m_data.p);

        if (texId) {
            this->ITexMarkAsUpdated(texId);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// state objects

ID3D11BlendState* CGxDeviceD3d11::IGetBlendState() {
    uint32_t mode = this->m_appRenderStates[GxRs_BlendingMode].m_value.m_data.u[0];

    if (mode >= GxBlends_Last) {
        mode = 0;
    }

    uint32_t mask = 0;

    if (this->m_appMasterEnables & 0x10) {
        mask = this->m_appRenderStates[GxRs_ColorWrite].m_value.m_data.u[0];
    }

    uint32_t writeMask = (mask & 0x1) != 0;

    if (mask & 0x4) {
        writeMask |= D3D11_COLOR_WRITE_ENABLE_GREEN;
    }

    if (mask & 0x2) {
        writeMask |= D3D11_COLOR_WRITE_ENABLE_BLUE;
    }

    if (mask & 0x8) {
        writeMask |= D3D11_COLOR_WRITE_ENABLE_ALPHA;
    }

    uint64_t key = mode | (static_cast<uint64_t>(writeMask) << 8);
    auto found = this->m_blendStates.find(key);

    if (found != this->m_blendStates.end()) {
        return found->second;
    }

    D3D11_BLEND_DESC desc = {};
    D3D11_RENDER_TARGET_BLEND_DESC& rt = desc.RenderTarget[0];
    rt.BlendEnable = mode >= 2;
    rt.SrcBlend = s_srcBlend[mode];
    rt.DestBlend = s_dstBlend[mode];
    rt.BlendOp = D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha = AlphaFactor(rt.SrcBlend);
    rt.DestBlendAlpha = AlphaFactor(rt.DestBlend);
    rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = static_cast<UINT8>(writeMask);

    ID3D11BlendState* state = nullptr;
    this->m_d3dDevice->CreateBlendState(&desc, &state);
    this->m_blendStates[key] = state;
    return state;
}

ID3D11DepthStencilState* CGxDeviceD3d11::IGetDepthState() {
    uint32_t func = 4;  // always

    if ((this->m_appMasterEnables & 0x4) && this->m_appRenderStates[GxRs_DepthTest].m_value.m_data.i[0]) {
        func = this->m_appRenderStates[GxRs_DepthFunc].m_value.m_data.u[0] & 3;
    }

    uint32_t write = 0;

    if (this->m_appMasterEnables & 0x8) {
        write = this->m_appRenderStates[GxRs_DepthWrite].m_value.m_data.i[0] != 0;
    }

    static const bool s_noDepth = GetEnvironmentVariableA("WOW_D3D11_NODEPTH", nullptr, 0) != 0;
    if (s_noDepth) {
        func = 4;
    }

    uint64_t key = func | (write << 4);
    auto found = this->m_depthStates.find(key);

    if (found != this->m_depthStates.end()) {
        return found->second;
    }

    D3D11_DEPTH_STENCIL_DESC desc = {};
    desc.DepthEnable = TRUE;
    desc.DepthFunc = func == 4 ? D3D11_COMPARISON_ALWAYS : s_cmpFunc[func];
    desc.DepthWriteMask = write ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;

    ID3D11DepthStencilState* state = nullptr;
    this->m_d3dDevice->CreateDepthStencilState(&desc, &state);
    this->m_depthStates[key] = state;
    return state;
}

ID3D11RasterizerState* CGxDeviceD3d11::IGetRasterState() {
    uint32_t cull = 0;

    if (this->m_appMasterEnables & 0x20) {
        cull = this->m_appRenderStates[GxRs_Culling].m_value.m_data.u[0];

        if (cull > 2) {
            cull = 2;
        }
    }

    uint32_t wireframe = (this->m_appMasterEnables & (1 << GxMasterEnable_PolygonFill)) ? 0 : 1;
    uint32_t scissor = this->m_appRenderStates[GxRs_ScissorTest].m_value.m_data.i[0] != 0;
    uint32_t multisample = this->m_sampleCount > 1 && this->m_appRenderStates[GxRs_Multisample].m_value.m_data.i[0] != 0;

    // D3D9 depth bias is in depth buffer units, D3D11 counts 24 bit steps
    float offset = this->m_appRenderStates[GxRs_PolygonOffset].m_value.m_data.f[0];
    int32_t bias = static_cast<int32_t>(-offset * 16777216.0f);

    static const bool s_noCull = GetEnvironmentVariableA("WOW_D3D11_NOCULL", nullptr, 0) != 0;
    if (s_noCull) {
        cull = 0;
    }

    uint64_t key = cull | (wireframe << 2) | (scissor << 3) | (multisample << 4) | (static_cast<uint64_t>(static_cast<uint32_t>(bias)) << 8);
    auto found = this->m_rasterStates.find(key);

    if (found != this->m_rasterStates.end()) {
        return found->second;
    }

    D3D11_RASTERIZER_DESC desc = {};
    desc.FillMode = wireframe ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
    // D3D9 CULL_CW removes clockwise triangles (front faces with the default winding), CULL_CCW removes counter-clockwise
    desc.CullMode = cull == 0 ? D3D11_CULL_NONE : (cull == 1 ? D3D11_CULL_FRONT : D3D11_CULL_BACK);
    desc.DepthBias = bias;
    desc.DepthClipEnable = TRUE;
    desc.ScissorEnable = scissor;
    desc.MultisampleEnable = multisample;

    ID3D11RasterizerState* state = nullptr;
    this->m_d3dDevice->CreateRasterizerState(&desc, &state);
    this->m_rasterStates[key] = state;
    return state;
}

ID3D11SamplerState* CGxDeviceD3d11::IGetSamplerState(uint32_t filter, uint32_t aniso, uint32_t wrapU, uint32_t wrapV) {
    if (filter == GxTex_Anisotropic && aniso < 2) {
        filter = GxTex_LinearMipLinear;
    }

    if (aniso > 16) {
        aniso = 16;
    }

    uint64_t key = filter | (wrapU << 4) | (wrapV << 5) | (aniso << 6);
    auto found = this->m_samplerStates.find(key);

    if (found != this->m_samplerStates.end()) {
        return found->second;
    }

    D3D11_SAMPLER_DESC desc = {};

    // min, mag, mip: 0 = point, 1 = linear. mip "none" additionally clamps the level range to the base level
    static const uint8_t table[GxTexFilters_Last][3] = { { 0, 0, 0 }, { 1, 1, 0 }, { 0, 0, 0 }, { 1, 1, 0 }, { 1, 1, 1 }, { 1, 1, 1 } };
    const uint8_t* f = table[filter];
    bool noMips = filter == GxTex_Nearest || filter == GxTex_Linear;

    if (filter == GxTex_Anisotropic) {
        desc.Filter = D3D11_FILTER_ANISOTROPIC;
    } else {
        desc.Filter = static_cast<D3D11_FILTER>((f[0] << 4) | (f[1] << 2) | f[2]);
    }

    desc.AddressU = wrapU ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.AddressV = wrapV ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.MaxAnisotropy = aniso ? aniso : 1;
    desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    desc.MinLOD = 0.0f;
    static const bool s_lod0 = GetEnvironmentVariableA("WOW_D3D11_LOD0", nullptr, 0) != 0;
    desc.MaxLOD = (noMips || s_lod0) ? 0.0f : D3D11_FLOAT32_MAX;

    ID3D11SamplerState* state = nullptr;
    this->m_d3dDevice->CreateSamplerState(&desc, &state);
    this->m_samplerStates[key] = state;
    return state;
}

// ---------------------------------------------------------------------------------------------
// viewport / scissor

void CGxDeviceD3d11::ISetViewport() {
    const CRect& window = this->DeviceCurWindow();
    D3D11_VIEWPORT viewport;

    uint32_t x = static_cast<uint32_t>(this->m_viewport.m_x.m_l * window.maxX + 0.5f);
    uint32_t y = static_cast<uint32_t>((1.0f - this->m_viewport.m_y.m_h) * window.maxY + 0.5f);
    uint32_t width = static_cast<uint32_t>(this->m_viewport.m_x.m_h * window.maxX - x + 0.5f);
    uint32_t height = static_cast<uint32_t>((1.0f - this->m_viewport.m_y.m_l) * window.maxY - y + 0.5f);

    if (this->m_textureTarget[0].m_apiSpecific || this->m_textureTarget[1].m_apiSpecific) {
        y = static_cast<uint32_t>(window.maxY * this->m_viewport.m_y.m_l);
    }

    viewport.TopLeftX = static_cast<float>(x);
    viewport.TopLeftY = static_cast<float>(y);
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = this->m_viewport.m_z.m_l;
    viewport.MaxDepth = this->m_viewport.m_z.m_h;

    this->m_d3dContext->RSSetViewports(1, &viewport);
    this->m_viewportDirty = 0;
}

void CGxDeviceD3d11::ISetScissor() {
    if (!this->m_scissorRectDirty) {
        return;
    }

    const CRect& window = this->DeviceCurWindow();
    D3D11_RECT rect;

    rect.left = static_cast<LONG>(this->m_scissorRect.minX * window.maxX + 0.5f);
    rect.right = static_cast<LONG>(this->m_scissorRect.maxX * window.maxX + 1.0f);

    if (this->m_textureTarget[0].m_apiSpecific || this->m_textureTarget[1].m_apiSpecific) {
        rect.top = static_cast<LONG>(this->m_scissorRect.minY * window.maxY + 0.5f);
        rect.bottom = static_cast<LONG>(1.0f + this->m_scissorRect.maxY * window.maxY);
    } else {
        rect.top = static_cast<LONG>((1.0f - this->m_scissorRect.maxY) * window.maxY + 0.5f);
        rect.bottom = static_cast<LONG>(1.0f + (1.0f - this->m_scissorRect.minY) * window.maxY);
    }

    if (rect.left < 0) {
        rect.left = 0;
    }

    if (rect.top < 0) {
        rect.top = 0;
    }

    if (rect.right > window.maxX) {
        rect.right = static_cast<LONG>(window.maxX);
    }

    if (rect.bottom > window.maxY) {
        rect.bottom = static_cast<LONG>(window.maxY);
    }

    this->m_d3dContext->RSSetScissorRects(1, &rect);
    this->m_scissorRectDirty = 0;
}

// ---------------------------------------------------------------------------------------------
// shaders and constants

CGxDeviceD3d11::ShaderData* CGxDeviceD3d11::IBoundVertexShader() {
    CGxShader* shader = static_cast<CGxShader*>(this->m_appRenderStates[GxRs_VertexShader].m_value.m_data.p);

    if (!shader) {
        return nullptr;
    }

    if (!shader->m_loaded) {
        this->IShaderCreateFor(shader);
    }

    return shader->m_valid ? static_cast<ShaderData*>(shader->m_apiSpecific) : nullptr;
}

CGxDeviceD3d11::ShaderData* CGxDeviceD3d11::IBoundPixelShader() {
    CGxShader* shader = static_cast<CGxShader*>(this->m_appRenderStates[GxRs_PixelShader].m_value.m_data.p);

    if (!shader) {
        return nullptr;
    }

    if (!shader->m_loaded) {
        this->IShaderCreateFor(shader);
    }

    return shader->m_valid ? static_cast<ShaderData*>(shader->m_apiSpecific) : nullptr;
}

void CGxDeviceD3d11::IShaderConstantsFlush() {
    for (uint32_t stage = 0; stage < 2; stage++) {
        ShadowConstants& constants = CGxDevice::s_shadowConstants[stage];

        if (this->m_constants[stage] && constants.dirtyMin <= constants.dirtyMax) {
            // like SetVertexShaderConstantF: only the dirty range is replaced, the rest keeps its previous value
            memcpy(&this->m_gpuConstants[stage][constants.dirtyMin * 4], &constants.constants[constants.dirtyMin * 4], (constants.dirtyMax - constants.dirtyMin + 1) * 4 * sizeof(float));
            this->m_d3dContext->UpdateSubresource(this->m_constants[stage], 0, nullptr, this->m_gpuConstants[stage], 0, 0);
        }

        constants.dirtyMin = ShadowConstants::Count - 1;
        constants.dirtyMax = 0;
    }

    // render state the shaders emulate: alpha test and user clip planes
    float extra[28] = {};
    int32_t alphaRef = this->m_appRenderStates[GxRs_AlphaRef].m_value.m_data.i[0];
    extra[0] = alphaRef > 0 ? 1.0f : 0.0f;
    extra[1] = alphaRef / 255.0f;

    static const bool s_magenta = GetEnvironmentVariableA("WOW_D3D11_MAGENTA", nullptr, 0) != 0;
    extra[2] = s_magenta ? 1.0f : 0.0f;

    static const float s_debugMode = []() {
        char buf[16];
        return GetEnvironmentVariableA("WOW_D3D11_UVDEBUG", buf, sizeof(buf)) ? static_cast<float>(atoi(buf)) : 0.0f;
    }();
    extra[3] = s_debugMode;

    uint32_t planeMask = this->m_appRenderStates[GxRs_ClipPlaneMask].m_value.m_data.u[0];

    for (uint32_t i = 0; i < 6; i++) {
        if (planeMask & (1u << i)) {
            extra[4 + i * 4 + 0] = this->m_clipPlane[i].n.x;
            extra[4 + i * 4 + 1] = this->m_clipPlane[i].n.y;
            extra[4 + i * 4 + 2] = this->m_clipPlane[i].n.z;
            extra[4 + i * 4 + 3] = this->m_clipPlane[i].d;
        }
    }

    if (this->m_logDraws > 36 && planeMask) {
        D3d11Log("   clip mask %x plane0 %g %g %g %g", planeMask, extra[4], extra[5], extra[6], extra[7]);
    }

    for (uint32_t stage = 0; stage < 2; stage++) {
        if (this->m_extraConstants[stage] && (this->m_extraDirty[stage] || memcmp(this->m_extra[stage], extra, sizeof(extra)))) {
            memcpy(this->m_extra[stage], extra, sizeof(extra));
            this->m_d3dContext->UpdateSubresource(this->m_extraConstants[stage], 0, nullptr, extra, 0, 0);
            this->m_extraDirty[stage] = 0;
        }
    }
}

void CGxDeviceD3d11::IBindTextures() {
    ShaderData* ps = this->IBoundPixelShader();

    ID3D11ShaderResourceView* srvs[16];
    ID3D11SamplerState* samplers[16];

    TexData* rtColor = this->m_textureTarget[GxBuffers_Color].m_texture ? static_cast<TexData*>(this->m_textureTarget[GxBuffers_Color].m_apiSpecific) : nullptr;
    TexData* rtDepth = this->m_textureTarget[GxBuffers_Depth].m_texture ? static_cast<TexData*>(this->m_textureTarget[GxBuffers_Depth].m_apiSpecific) : nullptr;

    for (uint32_t i = 0; i < 16; i++) {
        CGxTex* texId = static_cast<CGxTex*>(this->m_appRenderStates[GxRs_Texture0 + i].m_value.m_data.p);
        srvs[i] = nullptr;
        samplers[i] = nullptr;

        if (!texId) {
            continue;
        }

        this->ITexMarkAsUpdated(texId);
        TexData* data = this->ITexEnsure(texId);

        if (!data) {
            data = this->m_defaultGxTex ? static_cast<TexData*>(this->m_defaultGxTex->m_apiSpecificData) : nullptr;
        }

        static const bool s_texGreen = GetEnvironmentVariableA("WOW_D3D11_TEXGREEN", nullptr, 0) != 0;

        if (s_texGreen && this->m_defaultGxTex && this->m_defaultGxTex->m_apiSpecificData) {
            data = static_cast<TexData*>(this->m_defaultGxTex->m_apiSpecificData);
        }

        if (!data || data == rtColor || data == rtDepth) {
            continue;
        }

        srvs[i] = data->m_srv;
        samplers[i] = this->IGetSamplerState(texId->m_flags.m_filter, texId->m_flags.m_maxAnisotropy, texId->m_flags.m_wrapU, texId->m_flags.m_wrapV);
    }

    (void)ps;

    if (memcmp(srvs, this->m_curSrvs, sizeof(srvs))) {
        this->m_d3dContext->PSSetShaderResources(0, 16, srvs);
        memcpy(this->m_curSrvs, srvs, sizeof(srvs));
    }

    if (memcmp(samplers, this->m_curSamplers, sizeof(samplers))) {
        this->m_d3dContext->PSSetSamplers(0, 16, samplers);
        memcpy(this->m_curSamplers, samplers, sizeof(samplers));
    }
}

// ---------------------------------------------------------------------------------------------
// vertex input

ID3D11InputLayout* CGxDeviceD3d11::IGetInputLayout(const std::vector<D3d9ShaderInfo::Input>& inputs, ID3DBlob* blob, const D3D11_INPUT_ELEMENT_DESC* elements, uint32_t elementCount, uint64_t layoutHash) {
    (void)inputs;

    uint64_t key = layoutHash ^ (reinterpret_cast<uintptr_t>(blob) * 0x9E3779B97F4A7C15ull);
    auto found = this->m_inputLayouts.find(key);

    if (found != this->m_inputLayouts.end()) {
        return found->second;
    }

    ID3D11InputLayout* layout = nullptr;
    HRESULT hr = this->m_d3dDevice->CreateInputLayout(elements, elementCount, blob->GetBufferPointer(), blob->GetBufferSize(), &layout);

    if (FAILED(hr)) {
        D3d11Log("CGxDeviceD3d11: CreateInputLayout failed 0x%08X (%u elements)", hr, elementCount);
    }

    this->m_inputLayouts[key] = layout;
    return layout;
}

void CGxDeviceD3d11::IBindVertexInput(ShaderData* vs) {
    D3D11_INPUT_ELEMENT_DESC elements[GxVAs_Last + 16];
    uint32_t elementCount = 0;

    CGxBuf* streamBufs[GxVAs_Last] = {};
    uint32_t streamStrides[GxVAs_Last] = {};
    uint32_t streamCount = 0;

    bool singleStream = this->m_primVertexFormat < GxVertexBufferFormats_Last;
    uint64_t hash = 1469598103934665603ull;

    for (uint32_t i = 0; i < GxVAs_Last; i++) {
        if (!(this->m_primVertexMask & (1 << i)) || !this->m_primVertexFormatBuf[i]) {
            continue;
        }

        uint32_t stream;

        for (stream = 0; stream < streamCount; stream++) {
            if (streamBufs[stream] == this->m_primVertexFormatBuf[i]) {
                break;
            }
        }

        if (stream == streamCount) {
            if (streamCount >= 15) {
                continue;
            }

            streamBufs[streamCount++] = this->m_primVertexFormatBuf[i];
        }

        const CGxVertexAttrib& attrib = this->m_primVertexFormatAttrib[i];
        streamStrides[stream] += s_attribSize[attrib.m_type];

        D3D11_INPUT_ELEMENT_DESC& e = elements[elementCount++];
        e.SemanticName = s_attribSemantic[attrib.m_attrib];
        e.SemanticIndex = s_attribSemanticIndex[attrib.m_attrib];
        e.Format = s_attribToDxgi[attrib.m_type];
        e.InputSlot = stream;
        e.AlignedByteOffset = attrib.m_offset;
        e.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
        e.InstanceDataStepRate = 0;

        hash = (hash ^ (i | (attrib.m_type << 8) | (stream << 16) | (static_cast<uint64_t>(attrib.m_offset) << 24))) * 1099511628211ull;
    }

    if (singleStream && streamCount == 1) {
        streamStrides[0] = this->m_primVertexSize;
    }

    // vertex shader inputs the format does not provide read zeros from a stride-0 stream
    bool needNull = false;

    for (const D3d9ShaderInfo::Input& in : vs->m_inputs) {
        const char* name = D3d9UsageName(in.m_usage);
        bool provided = false;

        for (uint32_t e = 0; e < elementCount; e++) {
            if (elements[e].SemanticIndex == in.m_usageIndex && !_stricmp(elements[e].SemanticName, name) && elements[e].InputSlot != 15) {
                provided = true;
                break;
            }
        }

        if (!provided) {
            D3D11_INPUT_ELEMENT_DESC& e = elements[elementCount++];
            e.SemanticName = name;
            e.SemanticIndex = in.m_usageIndex;
            e.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            e.InputSlot = 15;
            e.AlignedByteOffset = 0;
            e.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
            e.InstanceDataStepRate = 0;
            hash = (hash ^ (0x100000 | in.m_usage | (in.m_usageIndex << 8))) * 1099511628211ull;
            needNull = true;
        }
    }

    if (this->m_logDraws > 36) {
        for (uint32_t e = 0; e < elementCount; e++) {
            D3d11Log("   elem %s%u fmt %d slot %u off %u", elements[e].SemanticName, elements[e].SemanticIndex, elements[e].Format, elements[e].InputSlot, elements[e].AlignedByteOffset);
        }

        for (uint32_t s = 0; s < streamCount; s++) {
            CGxBuf* b = streamBufs[s];
            PoolData* pd = b->m_pool ? static_cast<PoolData*>(b->m_pool->m_apiSpecific) : nullptr;
            const float* f = pd ? reinterpret_cast<const float*>(pd->m_shadow + b->m_index) : nullptr;
            D3d11Log("   stream %u stride %u bufindex %u size %u itemSize %u: %g %g %g %g %g %g %g %g %g %g %g %g | next vtx %g %g %g", s, streamStrides[s], b->m_index, b->m_size, b->m_itemSize, f ? f[0] : 0, f ? f[1] : 0, f ? f[2] : 0, f ? f[3] : 0, f ? f[4] : 0, f ? f[5] : 0, f ? f[6] : 0, f ? f[7] : 0, f ? f[8] : 0, f ? f[9] : 0, f ? f[10] : 0, f ? f[11] : 0, f ? f[streamStrides[s] / 4] : 0, f ? f[streamStrides[s] / 4 + 1] : 0, f ? f[streamStrides[s] / 4 + 2] : 0);
        }
    }

    static int32_t s_ffLayoutLogs = 0;

    if (vs == this->m_ff.m_vs && s_ffLayoutLogs < 3 && this->m_statFrames > 300) {
        s_ffLayoutLogs++;
        D3d11Log("FF layout (%u elements, mask %x):", elementCount, this->m_primVertexMask);

        for (uint32_t e = 0; e < elementCount; e++) {
            D3d11Log("   %s%u fmt %d slot %u off %u", elements[e].SemanticName, elements[e].SemanticIndex, elements[e].Format, elements[e].InputSlot, elements[e].AlignedByteOffset);
        }
    }

    ID3D11InputLayout* layout = this->IGetInputLayout(vs->m_inputs, vs->m_blob, elements, elementCount, hash);

    if (layout != this->m_curLayout) {
        this->m_d3dContext->IASetInputLayout(layout);
        this->m_curLayout = layout;
    }

    ID3D11Buffer* buffers[16] = {};
    UINT strides[16] = {};
    UINT offsets[16] = {};

    for (uint32_t s = 0; s < streamCount; s++) {
        CGxBuf* buf = streamBufs[s];
        PoolData* pool = buf->m_pool ? static_cast<PoolData*>(buf->m_pool->m_apiSpecific) : nullptr;
        buffers[s] = pool ? pool->m_buffer : nullptr;
        strides[s] = streamStrides[s];
        offsets[s] = buf->m_index;
    }

    if (needNull) {
        buffers[15] = this->m_nullVertexBuffer;
    }

    if (memcmp(buffers, this->m_curVertexBuffers, sizeof(buffers)) || memcmp(strides, this->m_curVertexStrides, sizeof(strides)) || memcmp(offsets, this->m_curVertexOffsets, sizeof(offsets))) {
        this->m_d3dContext->IASetVertexBuffers(0, 16, buffers, strides, offsets);
        memcpy(this->m_curVertexBuffers, buffers, sizeof(buffers));
        memcpy(this->m_curVertexStrides, strides, sizeof(strides));
        memcpy(this->m_curVertexOffsets, offsets, sizeof(offsets));
    }
}

// ---------------------------------------------------------------------------------------------
// draw

void CGxDeviceD3d11::IStateSync() {
    static const bool rebind = GetEnvironmentVariableA("WOW_D3D11_REBIND", nullptr, 0) != 0;
    if (rebind) {
        // Diagnostic control: reconstruct bindings from Gx without relying on
        // cached hardware state across offscreen and main-scene draws.
        this->m_curBlend = nullptr;
        this->m_curDepth = nullptr;
        this->m_curRaster = nullptr;
        this->m_curLayout = nullptr;
        this->m_curVs = nullptr;
        this->m_curPs = nullptr;
        this->m_curIndexBuffer = nullptr;
        this->m_curTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        memset(this->m_curVertexBuffers, 0, sizeof(this->m_curVertexBuffers));
        memset(this->m_curSrvs, 0, sizeof(this->m_curSrvs));
        memset(this->m_curSamplers, 0, sizeof(this->m_curSamplers));
        this->m_targetsDirty = 1;
        this->m_viewportDirty = 1;
        this->m_scissorRectDirty = 1;
    }
    this->IRsSync(0);
    this->ISetTargets();
    this->IShaderConstantsFlush();

    if (this->m_viewportDirty) {
        this->ISetViewport();
    }

    this->ISetScissor();

    ID3D11BlendState* blend = this->IGetBlendState();
    float blendFactor = this->m_appRenderStates[Unk84].m_value.m_data.f[0];
    float factor[4] = { blendFactor, blendFactor, blendFactor, blendFactor };

    if (blend != this->m_curBlend || blendFactor != this->m_curBlendFactor) {
        this->m_d3dContext->OMSetBlendState(blend, factor, 0xFFFFFFFF);
        this->m_curBlend = blend;
        this->m_curBlendFactor = blendFactor;
    }

    ID3D11DepthStencilState* depth = this->IGetDepthState();

    if (depth != this->m_curDepth) {
        this->m_d3dContext->OMSetDepthStencilState(depth, 0);
        this->m_curDepth = depth;
    }

    ID3D11RasterizerState* raster = this->IGetRasterState();

    if (raster != this->m_curRaster) {
        this->m_d3dContext->RSSetState(raster);
        this->m_curRaster = raster;
    }

    ShaderData* vs = this->IBoundVertexShader();
    ShaderData* ps = this->IBoundPixelShader();

    if (!vs || !ps) {
        this->IUpdateFixedFunctionConstants();
    }

    if (!vs) {
        vs = this->m_ff.m_vs;
    }

    if (!ps) {
        ps = this->m_ff.m_ps;
    }

    ID3D11VertexShader* vsObj = static_cast<ID3D11VertexShader*>(vs->m_shader);
    ID3D11PixelShader* psObj = static_cast<ID3D11PixelShader*>(ps->m_shader);

    if (vsObj != this->m_curVs) {
        this->m_d3dContext->VSSetShader(vsObj, nullptr, 0);
        this->m_curVs = vsObj;
    }

    if (psObj != this->m_curPs) {
        this->m_d3dContext->PSSetShader(psObj, nullptr, 0);
        this->m_curPs = psObj;
    }

    this->IBindTextures();
    this->IBindVertexInput(vs);
}

void CGxDeviceD3d11::IDrawFan(const CGxBatch& batch, int32_t indexed) {
    if (batch.m_count < 3) {
        return;
    }

    uint32_t triangles = batch.m_count - 2;
    std::vector<uint16_t> indices(triangles * 3);

    if (indexed) {
        PoolData* pool = this->m_primIndexBuf && this->m_primIndexBuf->m_pool ? static_cast<PoolData*>(this->m_primIndexBuf->m_pool->m_apiSpecific) : nullptr;

        if (!pool) {
            return;
        }

        const uint16_t* src = reinterpret_cast<const uint16_t*>(pool->m_shadow + this->m_primIndexBuf->m_index) + batch.m_start;

        for (uint32_t i = 0; i < triangles; i++) {
            indices[i * 3 + 0] = src[0];
            indices[i * 3 + 1] = src[i + 1];
            indices[i * 3 + 2] = src[i + 2];
        }
    } else {
        for (uint32_t i = 0; i < triangles; i++) {
            indices[i * 3 + 0] = 0;
            indices[i * 3 + 1] = static_cast<uint16_t>(i + 1);
            indices[i * 3 + 2] = static_cast<uint16_t>(i + 2);
        }
    }

    uint32_t bytes = static_cast<uint32_t>(indices.size() * sizeof(uint16_t));

    if (bytes > this->m_fanIndexBufferSize) {
        Rel(this->m_fanIndexBuffer);

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = bytes * 2;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        this->m_d3dDevice->CreateBuffer(&desc, nullptr, &this->m_fanIndexBuffer);
        this->m_fanIndexBufferSize = bytes * 2;
    }

    if (!this->m_fanIndexBuffer) {
        return;
    }

    D3D11_BOX box = { 0, 0, 0, bytes, 1, 1 };
    this->m_d3dContext->UpdateSubresource(this->m_fanIndexBuffer, 0, &box, indices.data(), 0, 0);
    this->m_d3dContext->IASetIndexBuffer(this->m_fanIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
    this->m_curIndexBuffer = this->m_fanIndexBuffer;
    this->m_curIndexOffset = 0;
    this->m_d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    this->m_curTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    this->m_d3dContext->DrawIndexed(triangles * 3, 0, 0);

    // the engine's own index buffer must be rebound by the next draw
    this->m_curIndexBuffer = nullptr;
}

void CGxDeviceD3d11::Draw(CGxBatch* batch, int32_t count) {
    if (!this->m_context || this->m_intF5C) {
        this->m_statSkipped++;
        return;
    }

    if (batch->m_primType >= GxPrims_Last || !batch->m_count) {
        return;
    }

    static int32_t s_skipFrom = -1;
    static int32_t s_skipTo = -1;
    static bool s_skipInit = false;

    if (!s_skipInit) {
        s_skipInit = true;
        char buf[64];

        if (GetEnvironmentVariableA("WOW_D3D11_SKIP", buf, sizeof(buf))) {
            sscanf(buf, "%d-%d", &s_skipFrom, &s_skipTo);
        }
    }

    if (static_cast<int32_t>(this->m_statDraws) >= s_skipFrom && static_cast<int32_t>(this->m_statDraws) <= s_skipTo) {
        this->m_statDraws++;
        return;
    }

    static const bool s_skipFF = GetEnvironmentVariableA("WOW_D3D11_SKIPFF", nullptr, 0) != 0;
    static const int32_t s_skipFFn = []() {
        char buf[16];
        return GetEnvironmentVariableA("WOW_D3D11_SKIPFFN", buf, sizeof(buf)) ? atoi(buf) : -1;
    }();

    if (s_skipFFn >= 0 && (!this->IBoundVertexShader() || !this->IBoundPixelShader()) && static_cast<int32_t>(this->m_statFfDraws) == s_skipFFn) {
        this->m_statDraws++;
        this->m_statFfDraws++;
        return;
    }

    if (s_skipFF && (!this->IBoundVertexShader() || !this->IBoundPixelShader())) {
        this->m_statDraws++;
        this->m_statFfDraws++;
        return;
    }

    this->IStateSync();
    this->m_statDraws++;

    static int32_t s_terrainLogs = 0;
    ShaderData* dbgVs = this->IBoundVertexShader();
    const bool dbgTerrain = dbgVs && !strncmp(dbgVs->m_name, "Terrain", 7);
    const bool dbgForce = dbgTerrain && s_terrainLogs < 40;

    if (dbgForce) {
        s_terrainLogs++;
    }

    static int32_t s_logFrom = -2;
    static int32_t s_logTo = -2;

    if (s_logFrom == -2) {
        char buf[64];
        s_logFrom = -1;
        s_logTo = -1;

        if (GetEnvironmentVariableA("WOW_D3D11_LOGN", buf, sizeof(buf))) {
            int a = -1;
            int b = -1;

            if (sscanf(buf, "%d-%d", &a, &b) == 2) {
                s_logFrom = a;
                s_logTo = b;
            } else if (sscanf(buf, "%d", &a) == 1) {
                s_logFrom = a;
                s_logTo = a;
            }
        }
    }

    const int32_t dbgDrawIndex = static_cast<int32_t>(this->m_statDraws) - 1;
    const bool dbgIndex = s_logFrom >= 0 && dbgDrawIndex >= s_logFrom && dbgDrawIndex <= s_logTo;

    if (this->m_logDraws > 0 || dbgForce || dbgIndex) {
        if (this->m_logDraws > 0) {
            this->m_logDraws--;
        }

        D3D11_VIEWPORT vp;
        UINT vpCount = 1;
        this->m_d3dContext->RSGetViewports(&vpCount, &vp);
        D3d11Log("draw prim #%d %d count %u start %u indexed %d | vp %.0f,%.0f %.0fx%.0f | rtv %p dsv %p | vs %s ps %s | vmask %05x fmt %d | blend %u cull %u depth %u/%u/%u", dbgDrawIndex, batch->m_primType, batch->m_count, batch->m_start, count, vp.TopLeftX, vp.TopLeftY, vp.Width, vp.Height, this->m_curRtv, this->m_curDsv, this->IBoundVertexShader() ? this->IBoundVertexShader()->m_name : "FF", this->IBoundPixelShader() ? this->IBoundPixelShader()->m_name : "FF", this->m_primVertexMask, this->m_primVertexFormat, this->m_appRenderStates[GxRs_BlendingMode].m_value.m_data.u[0], this->m_appRenderStates[GxRs_Culling].m_value.m_data.u[0], this->m_appRenderStates[GxRs_DepthTest].m_value.m_data.u[0], this->m_appRenderStates[GxRs_DepthFunc].m_value.m_data.u[0], this->m_appRenderStates[GxRs_DepthWrite].m_value.m_data.u[0]);
    }

    if (dbgForce || dbgIndex || this->m_logDraws > 30 || (this->m_statFrames >= 300 && this->m_logDraws >= 10)) {
        const float* v = this->m_gpuConstants[1];
        const float* p = this->m_gpuConstants[0];
        D3d11Log("   vs c3 %g %g %g %g c4 %g %g %g %g c5 %g %g %g %g c6 %g %g %g %g c7 %g %g %g %g c32 %g %g %g %g c33 %g %g %g %g", v[12], v[13], v[14], v[15], v[16], v[17], v[18], v[19], v[20], v[21], v[22], v[23], v[24], v[25], v[26], v[27], v[28], v[29], v[30], v[31], v[128], v[129], v[130], v[131], v[132], v[133], v[134], v[135]);
        D3d11Log("   vs c2 %g %g %g %g | c28 %g %g %g %g | c29 %g %g %g %g | c30 %g %g %g %g | c31 %g %g %g %g | ps c2 %g %g %g %g", v[8], v[9], v[10], v[11], v[112], v[113], v[114], v[115], v[116], v[117], v[118], v[119], v[120], v[121], v[122], v[123], v[124], v[125], v[126], v[127], p[8], p[9], p[10], p[11]);
    }

    if (dbgForce || dbgIndex || this->m_logDraws > 30 || (this->m_statFrames >= 300 && this->m_logDraws >= 10)) {
        for (uint32_t i = 0; i < 3; i++) {
            CGxTex* t = static_cast<CGxTex*>(this->m_appRenderStates[GxRs_Texture0 + i].m_value.m_data.p);

            if (t) {
                TexData* td = static_cast<TexData*>(t->m_apiSpecificData);
                D3d11Log("   tex%u %p %ux%u gxfmt %d dataFmt %d needsUpdate %d needsCreation %d data %p srv %p mips %u filter %u", i, t, t->m_width, t->m_height, t->m_format, t->m_dataFormat, t->m_needsUpdate, t->m_needsCreation, td, td ? td->m_srv : nullptr, td ? td->m_mips : 0, t->m_flags.m_filter);
            }
        }
    }

    if (!this->IBoundVertexShader() || !this->IBoundPixelShader()) {
        this->m_statFfDraws++;

        static int32_t s_ffLogs = 0;

        if ((s_ffLogs < 12 || dbgIndex) && this->m_primVertexFormat == GxVBF_PCT2) {
            for (uint32_t t = 0; t < 2; t++) {
                CGxTex* tx = static_cast<CGxTex*>(this->m_appRenderStates[GxRs_Texture0 + t].m_value.m_data.p);
                TexData* ftd = tx ? static_cast<TexData*>(tx->m_apiSpecificData) : nullptr;
                D3d11Log("   FF bound srv%u %p (tex srv %p) sampler %p", t, this->m_curSrvs[t], ftd ? ftd->m_srv : nullptr, this->m_curSamplers[t]);
                D3d11Log("   FF PCT2 tex%u %p %ux%u rt %d filter %u aniso %u mips %u needsUpdate %d userFunc %p", t, tx, tx ? tx->m_width : 0, tx ? tx->m_height : 0, tx ? tx->m_flags.m_renderTarget : 0, tx ? tx->m_flags.m_filter : 0, tx ? tx->m_flags.m_maxAnisotropy : 0, ftd ? ftd->m_mips : 0, tx ? tx->m_needsUpdate : 0, tx ? reinterpret_cast<void*>(tx->m_userFunc) : nullptr);
            }

            CGxBuf* vb = this->m_primVertexBuf;
            PoolData* vpd = vb && vb->m_pool ? static_cast<PoolData*>(vb->m_pool->m_apiSpecific) : nullptr;

            if (vpd) {
                const uint8_t* base = vpd->m_shadow + vb->m_index;
                for (uint32_t v = 0; v < 4; v++) {
                    const float* f = reinterpret_cast<const float*>(base + v * this->m_primVertexSize);
                    D3d11Log("   vtx%u pos %g %g %g color %08x tex0 %g %g tex1 %g %g (stride %u)", v, f[0], f[1], f[2], *reinterpret_cast<const uint32_t*>(&f[3]), f[4], f[5], f[6], f[7], this->m_primVertexSize);
                }
            }
        }

        if (s_ffLogs++ < 12 || dbgIndex) {
            D3d11Log("FF draw #%d: ps %s | prim %d count %u idx %d vmask %05x fmt %d blend %u lighting %d matDiffuse %08x alphaRef %d vs %s ps %s tex0 %p colorop0 %u alphaop0 %u fog %d | world %g %g %g %g | proj diag %g %g %g", dbgDrawIndex, this->IBoundPixelShader() ? this->IBoundPixelShader()->m_name : "FF", batch->m_primType, batch->m_count, count, this->m_primVertexMask, this->m_primVertexFormat, this->m_appRenderStates[GxRs_BlendingMode].m_value.m_data.u[0], this->m_appRenderStates[GxRs_Lighting].m_value.m_data.i[0], this->m_appRenderStates[GxRs_MatDiffuse].m_value.m_data.u[0], this->m_appRenderStates[GxRs_AlphaRef].m_value.m_data.i[0], this->IBoundVertexShader() ? "shader" : "FF", this->IBoundPixelShader() ? "shader" : "FF", this->m_appRenderStates[GxRs_Texture0].m_value.m_data.p, this->m_appRenderStates[GxRs_ColorOp0].m_value.m_data.u[0], this->m_appRenderStates[GxRs_AlphaOp0].m_value.m_data.u[0], this->m_appRenderStates[GxRs_Fog].m_value.m_data.i[0], this->m_xforms[GxXform_World].m_mtx[0].a0, this->m_xforms[GxXform_World].m_mtx[0].b1, this->m_xforms[GxXform_World].m_mtx[0].c2, this->m_xforms[GxXform_World].m_mtx[0].d3, this->m_projNative.a0, this->m_projNative.b1, this->m_projNative.c2);
        }
    }

    if (count) {
        PoolData* pool = this->m_primIndexBuf && this->m_primIndexBuf->m_pool ? static_cast<PoolData*>(this->m_primIndexBuf->m_pool->m_apiSpecific) : nullptr;

        if (!pool || !pool->m_buffer) {
            this->m_statSkipped++;
            return;
        }

        if (batch->m_primType == GxPrim_TriangleFan) {
            this->IDrawFan(*batch, 1);
            return;
        }

        uint32_t offset = this->m_primIndexBuf->m_index;

        if (pool->m_buffer != this->m_curIndexBuffer || offset != this->m_curIndexOffset) {
            this->m_d3dContext->IASetIndexBuffer(pool->m_buffer, DXGI_FORMAT_R16_UINT, offset);
            this->m_curIndexBuffer = pool->m_buffer;
            this->m_curIndexOffset = offset;
        }

        D3D11_PRIMITIVE_TOPOLOGY topology = s_primTopology[batch->m_primType];

        if (topology != this->m_curTopology) {
            this->m_d3dContext->IASetPrimitiveTopology(topology);
            this->m_curTopology = topology;
        }

        this->m_d3dContext->DrawIndexed(batch->m_count, batch->m_start, 0);
    } else {
        if (batch->m_primType == GxPrim_TriangleFan) {
            this->IDrawFan(*batch, 0);
            return;
        }

        D3D11_PRIMITIVE_TOPOLOGY topology = s_primTopology[batch->m_primType];

        if (topology != this->m_curTopology) {
            this->m_d3dContext->IASetPrimitiveTopology(topology);
            this->m_curTopology = topology;
        }

        this->m_d3dContext->Draw(batch->m_count, 0);
    }
}
