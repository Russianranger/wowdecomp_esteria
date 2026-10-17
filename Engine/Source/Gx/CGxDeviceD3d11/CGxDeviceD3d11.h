#ifndef ENGINE_SOURCE_GX_CGXDEVICED3D11_CGXDEVICED3D11_H
#define ENGINE_SOURCE_GX_CGXDEVICED3D11_CGXDEVICED3D11_H

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxDeviceD3d11/ShaderTranslator.h"
#include "Engine/Source/Storm/H/stpl.h"

// Direct3D 11 backend for the 3.3.5a Gx layer.
//
// The engine drives a D3D9 style device (fixed function pipeline, SM2/SM3 shaders). This
// backend keeps that interface and emulates the missing parts:
//  - .bls shader bytecode (SM2/SM3) is translated to HLSL and compiled at first use
//  - the fixed function pipeline (transform, lighting, texture stages, fog) is one uber shader
//  - D3D9 alpha test and user clip planes are applied by the shaders from a constant buffer
//  - buffers are kept in system memory shadows and uploaded with UpdateSubresource on unlock
// Immediately flushed diagnostics (Logs\d3d11.log); the Gx log is buffered and lost on a crash
void D3d11Log(const char* format, ...);

class CGxDeviceD3d11 : public CGxDevice {
    public:
    struct TexData {
        ID3D11Texture2D* m_texture = nullptr;
        ID3D11ShaderResourceView* m_srv = nullptr;
        ID3D11RenderTargetView* m_rtv[6] = {};
        ID3D11DepthStencilView* m_dsv = nullptr;
        DXGI_FORMAT m_format = DXGI_FORMAT_UNKNOWN;
        uint32_t m_width = 0;
        uint32_t m_height = 0;
        uint32_t m_mips = 1;
        uint32_t m_faces = 1;
    };

    struct PoolData {
        ID3D11Buffer* m_buffer = nullptr;
        uint8_t* m_shadow = nullptr;
        uint32_t m_size = 0;
    };

    struct ShaderData {
        ID3D11DeviceChild* m_shader = nullptr;
        ID3DBlob* m_blob = nullptr;
        std::vector<D3d9ShaderInfo::Input> m_inputs;
        char m_name[48] = {};
    };

    // Everything the draw path needs from the fixed function emulation
    struct FixedFunction {
        ShaderData* m_vs = nullptr;
        ShaderData* m_ps = nullptr;
        ID3D11Buffer* m_vsConstants = nullptr;
        ID3D11Buffer* m_psConstants = nullptr;
        ShaderData* m_blitVs = nullptr;
        ShaderData* m_blitPs = nullptr;
        ID3D11Buffer* m_blitConstants = nullptr;
    };

    struct DepthTarget {
        ID3D11Texture2D* m_texture = nullptr;
        ID3D11DepthStencilView* m_dsv = nullptr;
        uint32_t m_width = 0;
        uint32_t m_height = 0;
    };

    HWND m_hwnd = nullptr;
    ATOM m_hwndClass = 0;
    int32_t m_ownhwnd = 0;
    ID3D11Device* m_d3dDevice = nullptr;
    ID3D11DeviceContext* m_d3dContext = nullptr;
    IDXGIFactory1* m_dxgiFactory = nullptr;
    IDXGISwapChain* m_swapChain = nullptr;
    D3D_FEATURE_LEVEL m_featureLevel = D3D_FEATURE_LEVEL_10_0;

    // default render targets
    ID3D11Texture2D* m_backBuffer = nullptr;
    ID3D11ShaderResourceView* m_backBufferSrv = nullptr;
    ID3D11RenderTargetView* m_backBufferRtv = nullptr;
    ID3D11Texture2D* m_msaaColor = nullptr;
    ID3D11RenderTargetView* m_msaaColorRtv = nullptr;
    DepthTarget m_defDepth;
    DepthTarget m_scratchDepth;
    DXGI_FORMAT m_backBufferFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    DXGI_FORMAT m_depthFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    uint32_t m_sampleCount = 1;
    uint32_t m_sampleQuality = 0;
    uint32_t m_backBufferWidth = 0;
    uint32_t m_backBufferHeight = 0;
    TexData* m_depthOverride = nullptr;

    // constants
    ID3D11Buffer* m_constants[2] = {};  // 0 = pixel, 1 = vertex (matches s_shadowConstants)
    ID3D11Buffer* m_extraConstants[2] = {};
    float m_extra[2][28] = {};
    float m_gpuConstants[2][ShadowConstants::Count * 4] = {};
    float m_curBlendFactor = 1.0f;
    int32_t m_extraDirty[2] = { 1, 1 };

    // pipeline state caches
    std::unordered_map<uint64_t, ID3D11BlendState*> m_blendStates;
    std::unordered_map<uint64_t, ID3D11DepthStencilState*> m_depthStates;
    std::unordered_map<uint64_t, ID3D11RasterizerState*> m_rasterStates;
    std::unordered_map<uint64_t, ID3D11SamplerState*> m_samplerStates;
    std::unordered_map<uint64_t, ID3D11InputLayout*> m_inputLayouts;
    std::unordered_map<std::string, ShaderData*> m_shaderCache;

    ID3D11BlendState* m_curBlend = nullptr;
    ID3D11DepthStencilState* m_curDepth = nullptr;
    ID3D11RasterizerState* m_curRaster = nullptr;
    ID3D11InputLayout* m_curLayout = nullptr;
    ID3D11VertexShader* m_curVs = nullptr;
    ID3D11PixelShader* m_curPs = nullptr;
    ID3D11Buffer* m_curIndexBuffer = nullptr;
    uint32_t m_curIndexOffset = 0;
    ID3D11Buffer* m_curVertexBuffers[16] = {};
    uint32_t m_curVertexOffsets[16] = {};
    uint32_t m_curVertexStrides[16] = {};
    ID3D11ShaderResourceView* m_curSrvs[16] = {};
    ID3D11SamplerState* m_curSamplers[16] = {};
    ID3D11RenderTargetView* m_curRtv = nullptr;
    ID3D11DepthStencilView* m_curDsv = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY m_curTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    int32_t m_targetsDirty = 1;

    uint32_t m_statFrames = 0;
    uint32_t m_statDraws = 0;
    uint32_t m_statFfDraws = 0;
    uint32_t m_statSkipped = 0;
    int32_t m_logDraws = 0;

    ID3D11Buffer* m_nullVertexBuffer = nullptr;
    ID3D11Buffer* m_fanIndexBuffer = nullptr;
    uint32_t m_fanIndexBufferSize = 0;

    FixedFunction m_ff;
    TexData* m_defaultTex = nullptr;
    CGxTex* m_defaultGxTex = nullptr;
    CImVector m_clearColor;

    // static tables
    static const DXGI_FORMAT s_texFormatToDxgi[GxTexFormats_Last];
    static EGxTexFormat s_texFormatToUse[GxTexFormats_Last];
    static const D3D11_PRIMITIVE_TOPOLOGY s_primTopology[GxPrims_Last];
    static const DXGI_FORMAT s_attribToDxgi[7];
    static const uint32_t s_attribSize[7];
    static const char* const s_attribSemantic[GxVAs_Last];
    static const uint32_t s_attribSemanticIndex[GxVAs_Last];

    virtual void ITexMarkAsUpdated(CGxTex* texId);
    virtual void IRsSendToHw(EGxRenderState which);
    virtual void ICursorCreate(const CGxFormat& format);
    virtual void ICursorDestroy();
    virtual void ICursorDraw();
    virtual ~CGxDeviceD3d11();
    virtual int32_t DeviceCreate(GxWindowProc windowProc, const CGxFormat& format);
    virtual int32_t DeviceCreate(void* window, const CGxFormat& format);
    virtual void DeviceDestroy();
    virtual void DeviceEvictResources();
    virtual int32_t DeviceSetFormat(const CGxFormat& format);
    virtual void DeviceSetBaseMipLevel(uint32_t level);
    virtual void DeviceSetGamma(float gamma);
    virtual void DeviceSetGamma(const CGxGammaRamp& ramp);
    virtual void* DeviceWindow();
    virtual void DeviceReadPixels(CiRect& rect, TSGrowableArray<CImVector>& pixels);
    virtual void DeviceReadDepths(CiRect& rect, TSGrowableArray<float>& depths);
    virtual int32_t DeviceWM(EGxWM wm, uintptr_t param1, uintptr_t param2);
    virtual void DeviceSetRenderTarget(EGxBuffer buffer, CGxTex* texture, uint32_t plane);
    virtual void DeviceSaveDepthSurface();
    virtual void DeviceRestoreDepthSurface();
    virtual void DeviceResolveDepthBuffer(CGxTex* texture);
    virtual void DeviceCopyTex(CGxTex* source, uint32_t sourcePlane, CGxTex* dest, uint32_t destPlane);
    virtual void DeviceOverride(EGxOverride override, uint32_t value);
    virtual void CapsWindowSize(CRect& rect);
    virtual void CapsWindowSizeInScreenCoords(CRect& rect);
    virtual void LogCrashInfo(char* buffer, uint32_t size);
    virtual void ScenePresent();
    virtual void SceneClear(uint32_t mask, CImVector color);
    virtual void XformSetProjection(const C44Matrix& matrix);
    virtual void XformSetView(const C44Matrix& matrix);
    virtual void Draw(CGxBatch* batch, int32_t count);
    virtual void PoolSizeSet(CGxPool* pool, uint32_t size);
    virtual void PoolDestroy(CGxPool* pool);
    virtual char* BufLock(CGxBuf* buf);
    virtual int32_t BufUnlock(CGxBuf* buf, uint32_t size);
    virtual void BufData(CGxBuf* buf, const void* data, uint32_t size, uint32_t offset);
    virtual void TexDestroy(CGxTex* texId);
    virtual int32_t TexCopy(CGxTex* dest, const C2iVector& destPos, const C2iVector& sourcePos, const C2iVector& size, uint32_t level, uint32_t plane);
    virtual int32_t TexStretch(CGxTex* dest, const CiRect* destRect, const CiRect* sourceRect, uint32_t level, uint32_t face);
    virtual void QueryCreate(CGxQuery*& query, EGxQueryType type);
    virtual void QueryDestroy(CGxQuery*& query);
    virtual bool QueryBegin(CGxQuery* query);
    virtual bool QueryEnd(CGxQuery* query);
    virtual bool QueryGetParam(CGxQuery* query, EGxQueryParam param, uint32_t& data);
    virtual bool QueryGetData(CGxQuery* query, uint32_t& data);
    virtual void ShaderCreate(CGxShader** shaders, EGxShTarget target, const char* path, const char* name, uint32_t permutations);
    virtual void ShaderDestroy(CGxShader*& shader);
    virtual void IShaderReload(CGxShader* shader, const char* path, const char* name);
    virtual void IShaderCreate(CGxShader* shader);
    virtual void CursorSetVisible(int32_t visible);
    virtual void CursorUnlock(uint32_t x, uint32_t y);
    virtual void StereoSetConvergence(float convergence);
    virtual float StereoGetConvergence();
    virtual void StereoSetSeparation(float separation);
    virtual float StereoGetSeparation();
    virtual bool StereoEnabled();
    virtual void DeviceUnk81(uint32_t a, uint32_t b);
    virtual void DeviceUnk82();
    virtual void DeviceUnk83(uint32_t a, uint32_t b);

    static LRESULT CALLBACK WindowProcD3d11(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    CGxDeviceD3d11();

    // device.cpp
    int32_t ICreateWindow(CGxFormat& format);
    int32_t ICreateD3dDevice(const CGxFormat& format);
    int32_t ICreateSwapChain(const CGxFormat& format);
    int32_t ICreateTargets();
    void IReleaseTargets();
    void IDestroyD3d();
    void ISetCaps(const CGxFormat& format);
    void ISetWindowFocus(int32_t focus);
    void IResize();
    DepthTarget* IGetScratchDepth(uint32_t width, uint32_t height);
    void ISetTargets();
    ID3D11Texture2D* IResolvedBackBuffer();
    int32_t IBlitBackBuffer(TexData* dst, uint32_t face, uint32_t level, const RECT* srcRect, const RECT* dstRect, bool linear);
    int32_t IBlit(ID3D11ShaderResourceView* srcSrv, uint32_t srcWidth, uint32_t srcHeight, const RECT* srcRect, TexData* dst, uint32_t face, uint32_t level, const RECT* dstRect, bool linear);

    // resources.cpp
    ID3D11Buffer* ICreateBuffer(uint32_t size, uint32_t bindFlags);
    void IReleasePool(CGxPool* pool);
    int32_t ICreatePoolApi(CGxPool* pool);
    char* IBufLock(CGxBuf* buf);
    void IBufUnlock(CGxBuf* buf);
    void IReleaseResources(int32_t freeTextures);
    int32_t ITexCreate(CGxTex* texId);
    void ITexUpload(CGxTex* texId);
    void ITexReleaseData(CGxTex* texId);
    void ITexForceRecreation(int32_t all);
    TexData* ITexEnsure(CGxTex* texId);
    ShaderData* ICompileShader(const std::string& hlsl, const char* target, const D3d9ShaderInfo& info, const char* name);
    void IShaderCreateFor(CGxShader* shader);
    void IReleaseShaders();
    void IReleaseShaderData(CGxShader* shader);
    void IReleaseStateCaches();

    // state.cpp
    void IStateSync();
    void IShaderConstantsFlush();
    ID3D11BlendState* IGetBlendState();
    ID3D11DepthStencilState* IGetDepthState();
    ID3D11RasterizerState* IGetRasterState();
    ID3D11SamplerState* IGetSamplerState(uint32_t filter, uint32_t aniso, uint32_t wrapU, uint32_t wrapV);
    ID3D11InputLayout* IGetInputLayout(const std::vector<D3d9ShaderInfo::Input>& inputs, ID3DBlob* blob, const D3D11_INPUT_ELEMENT_DESC* elements, uint32_t elementCount, uint64_t layoutHash);
    void ISetViewport();
    void ISetScissor();
    void IBindTextures();
    void IBindVertexInput(ShaderData* vs);
    void IDrawFan(const CGxBatch& batch, int32_t indexed);

    // fixedfunction.cpp
    int32_t ICreateFixedFunction();
    void IDestroyFixedFunction();
    void IUpdateFixedFunctionConstants();

    ShaderData* IBoundVertexShader();
    ShaderData* IBoundPixelShader();
};

CGxDevice* CreateCGxDeviceD3d11();

#endif
