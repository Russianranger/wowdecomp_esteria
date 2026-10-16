#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "Engine/Source/Gx/CGxDeviceD3d11/CGxDeviceD3d11.h"
#include "Engine/Source/Gx/Blit.h"
#include "Engine/Source/Gx/CGxBuf.h"
#include "Engine/Source/Gx/CGxPool.h"
#include "Engine/Source/Gx/CGxTex.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Os/Error.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/Storm/H/storm.h"

const DXGI_FORMAT CGxDeviceD3d11::s_texFormatToDxgi[GxTexFormats_Last] = {
    DXGI_FORMAT_UNKNOWN,
    DXGI_FORMAT_R8G8B8A8_UNORM,
    DXGI_FORMAT_B8G8R8A8_UNORM,
    DXGI_FORMAT_B4G4R4A4_UNORM,
    DXGI_FORMAT_B5G5R5A1_UNORM,
    DXGI_FORMAT_B5G6R5_UNORM,
    DXGI_FORMAT_BC1_UNORM,
    DXGI_FORMAT_BC2_UNORM,
    DXGI_FORMAT_BC3_UNORM,
    DXGI_FORMAT_R8G8_SNORM,
    DXGI_FORMAT_R16G16_FLOAT,
    DXGI_FORMAT_R32_FLOAT,
    DXGI_FORMAT_R24G8_TYPELESS,
};

// formats the engine asks for -> formats actually created (relaxed in ISetCaps for formats the GPU lacks)
EGxTexFormat CGxDeviceD3d11::s_texFormatToUse[GxTexFormats_Last] = { GxTex_Unknown, GxTex_Abgr8888, GxTex_Argb8888, GxTex_Argb4444, GxTex_Argb1555, GxTex_Rgb565, GxTex_Dxt1, GxTex_Dxt3, GxTex_Dxt5, GxTex_Uv88, GxTex_Gr1616F, GxTex_R32F, GxTex_D24X8 };

const D3D11_PRIMITIVE_TOPOLOGY CGxDeviceD3d11::s_primTopology[GxPrims_Last] = {
    D3D11_PRIMITIVE_TOPOLOGY_POINTLIST,
    D3D11_PRIMITIVE_TOPOLOGY_LINELIST,
    D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP,
    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,  // fans are expanded to lists
};

// Gx vertex attribute types: color, ubyte4, ubyte4n, float2, float3, short2, float1
const DXGI_FORMAT CGxDeviceD3d11::s_attribToDxgi[7] = {
    DXGI_FORMAT_B8G8R8A8_UNORM,
    DXGI_FORMAT_R8G8B8A8_UNORM,  // UBYTE4 bone indices: scaled back to 0..255 by the translated vertex shader
    DXGI_FORMAT_R8G8B8A8_UNORM,
    DXGI_FORMAT_R32G32_FLOAT,
    DXGI_FORMAT_R32G32B32_FLOAT,
    DXGI_FORMAT_R16G16_SINT,
    DXGI_FORMAT_R32_FLOAT,
};

const uint32_t CGxDeviceD3d11::s_attribSize[7] = { 4, 4, 4, 8, 12, 4, 4 };

// the D3D9 declaration usages the translated shaders declare their inputs with
const char* const CGxDeviceD3d11::s_attribSemantic[GxVAs_Last] = { "POSITION", "BLENDWEIGHT", "BLENDINDICES", "NORMAL", "COLOR", "COLOR", "TEXCOORD", "TEXCOORD", "TEXCOORD", "TEXCOORD", "TEXCOORD", "TEXCOORD", "TEXCOORD", "TEXCOORD" };
const uint32_t CGxDeviceD3d11::s_attribSemanticIndex[GxVAs_Last] = { 0, 0, 0, 0, 0, 1, 0, 1, 2, 3, 4, 5, 6, 7 };

static bool s_d3d11Debug = false;


void D3d11Log(const char* format, ...) {
    static FILE* s_file = nullptr;

    if (!s_file) {
        CreateDirectoryA("Logs", nullptr);
        s_file = fopen("Logs\\d3d11.log", "w");
    }

    if (!s_file) {
        return;
    }

    va_list args;
    va_start(args, format);
    vfprintf(s_file, format, args);
    va_end(args);
    fputc('\n', s_file);
    fflush(s_file);
}

static uint32_t SubresourceCount(uint32_t mips, uint32_t faces) {
    return mips * faces;
}

CGxDeviceD3d11::CGxDeviceD3d11() {
    this->m_api = GxApi_D3d11;
    this->m_caps.m_colorFormat = GxCF_argb;
    this->m_caps.unk_0018 = 1;
    this->DeviceCreatePools();
    this->DeviceCreateStreamBufs();
}

CGxDeviceD3d11::~CGxDeviceD3d11() {
}

CGxDevice* CreateCGxDeviceD3d11() {
    return new (SMemAlloc(sizeof(CGxDeviceD3d11), __FILE__, __LINE__, 0)) CGxDeviceD3d11();
}

// ---------------------------------------------------------------------------------------------
// window

static ATOM WindowClassCreate() {
    HINSTANCE instance = GetModuleHandleA(nullptr);

    WNDCLASSEXA windowClass;
    memset(&windowClass, 0, sizeof(windowClass));
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_OWNDC;
    windowClass.lpfnWndProc = CGxDeviceD3d11::WindowProcD3d11;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = "GxWindowClassD3d11";
    windowClass.hIcon = static_cast<HICON>(LoadImageA(instance, "BlizzardIcon.ico", IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    windowClass.hCursor = LoadCursorA(instance, "BlizzardCursor.cur");

    if (!windowClass.hCursor) {
        windowClass.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    }

    return RegisterClassExA(&windowClass);
}

int32_t CGxDeviceD3d11::ICreateWindow(CGxFormat& format) {
    HINSTANCE instance = GetModuleHandleA(nullptr);
    DWORD style;

    if (!format.m_window) {
        style = WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_SYSMENU;
    } else if (format.m_maximize == 1) {
        style = WS_POPUP | WS_VISIBLE;
    } else if (format.m_maximize == 2) {
        style = WS_POPUP;
    } else {
        style = WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    }

    float aspectRatio = 0.0f;

    if (format.m_aspect) {
        int32_t width = format.m_size.x;

        if (width) {
            int32_t height = format.m_size.y;

            if (height) {
                aspectRatio = static_cast<float>(width) / height;
            }
        }
    }

    RECT clientRect;
    clientRect.right = format.m_size.x;
    clientRect.left = 0;
    clientRect.top = 0;
    clientRect.bottom = format.m_size.y;
    AdjustWindowRectEx(&clientRect, style, FALSE, 0);

    CGxDevice::s_clientAdjust.x = clientRect.right - format.m_size.x - clientRect.left;
    CGxDevice::s_clientAdjust.y = clientRect.bottom - format.m_size.y - clientRect.top;

    if (format.m_window) {
        if (format.m_maximize == 1) {
            format.m_pos.x = 0;
            format.m_pos.y = 0;
            format.m_size.x = GetSystemMetrics(SM_CXSCREEN);
            format.m_size.y = GetSystemMetrics(SM_CYSCREEN);
        } else {
            RECT windowRect;
            windowRect.left = 0;
            windowRect.top = 0;
            windowRect.right = format.m_size.x;
            windowRect.bottom = format.m_size.y;

            if (CGxDevice::AdjustWindowRectToWorkArea(aspectRatio, &CGxDevice::s_clientAdjust, &windowRect)) {
                format.m_pos.x = windowRect.left;
                format.m_pos.y = windowRect.top;
                format.m_size.x = windowRect.right - windowRect.left;
                format.m_size.y = windowRect.bottom - windowRect.top;
            } else {
                format.m_pos.x = 0;
                format.m_pos.y = 0;
            }
        }
    }

    int32_t width = format.m_size.x;
    int32_t height = format.m_size.y;

    if (!width) {
        width = CW_USEDEFAULT;
    }

    if (!height) {
        height = CW_USEDEFAULT;
    }

    if (format.m_window && format.m_maximize != 1 && format.m_size.x && format.m_size.y) {
        width += CGxDevice::s_clientAdjust.x;
        height += CGxDevice::s_clientAdjust.y;
    }

    this->m_hwnd = CreateWindowExA(WS_EX_APPWINDOW, "GxWindowClassD3d11", "World of Warcraft", style, format.m_pos.x, format.m_pos.y, width, height, nullptr, nullptr, instance, this);

    if (this->m_hwnd && format.m_maximize != 2) {
        ShowWindow(this->m_hwnd, SW_SHOWNORMAL);
    }

    return this->m_hwnd != nullptr;
}

LRESULT CALLBACK CGxDeviceD3d11::WindowProcD3d11(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    CGxDeviceD3d11* device = reinterpret_cast<CGxDeviceD3d11*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));

    switch (message) {
        case WM_CREATE:
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTA*>(lparam)->lpCreateParams));
            return 0;

        case WM_DESTROY:
            if (device) {
                device->DeviceWM(GxWM_Destroy, 0, 0);
            }
            return 0;

        case WM_SIZE: {
            if (!device) {
                break;
            }

            CRect rect;
            rect.minY = 0.0f;
            rect.minX = 0.0f;
            rect.maxY = HIWORD(lparam);
            rect.maxX = LOWORD(lparam);

            uint32_t resizeType = 0;

            if (wparam == SIZE_MINIMIZED) {
                resizeType = 1;
            } else if (wparam == SIZE_MAXHIDE) {
                resizeType = 2;
            }

            device->DeviceWM(GxWM_Size, reinterpret_cast<uintptr_t>(&rect), resizeType);
            break;
        }

        case WM_ACTIVATE:
            if (device && LOWORD(wparam) == WA_INACTIVE) {
                // borderless "fullscreen": nothing to release on focus loss
            }
            break;

        case WM_SETFOCUS:
            if (device) {
                device->DeviceWM(GxWM_SetFocus, 0, 0);
            }
            return 0;

        case WM_KILLFOCUS:
            if (device) {
                device->DeviceWM(GxWM_KillFocus, 0, 0);
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT paint;
            BeginPaint(hwnd, &paint);
            EndPaint(hwnd, &paint);
            return 0;
        }

        case WM_ERASEBKGND:
            return 0;

        case WM_SETCURSOR:
            if (!device) {
                return DefWindowProcA(hwnd, message, wparam, lparam);
            }

            break;

        case WM_DISPLAYCHANGE:
            if (device && !device->IDevIsWindowed()) {
                CRect rect;
                rect.minY = 0.0f;
                rect.minX = 0.0f;
                rect.maxY = HIWORD(lparam);
                rect.maxX = LOWORD(lparam);
                device->DeviceWM(GxWM_DisplayChange, reinterpret_cast<uintptr_t>(&rect), 0);
            }

            break;

        case WM_SYSCOMMAND:
            if (wparam == SC_SCREENSAVE || wparam == SC_MONITORPOWER) {
                return 0;
            }

            break;

        case WM_SIZING: {
            RECT* sizingRect = reinterpret_cast<RECT*>(lparam);

            CiRect rect;
            rect.minX = sizingRect->left;
            rect.minY = sizingRect->top;
            rect.maxX = sizingRect->right;
            rect.maxY = sizingRect->bottom;

            if (rect.maxX - rect.minX <= 0 || rect.maxY - rect.minY <= 0) {
                return 0;
            }

            switch (wparam) {
                case WMSZ_LEFT:
                    CGxDevice::ClampRectLeft(rect);
                    CGxDevice::ClampRectTopByAspect(rect);
                    break;

                case WMSZ_RIGHT:
                    CGxDevice::ClampRectRight(rect);
                    CGxDevice::ClampRectBottomByAspect(rect);
                    break;

                case WMSZ_TOP:
                    CGxDevice::ClampRectTop(rect);
                    CGxDevice::ClampRectLeftByAspect(rect);
                    break;

                case WMSZ_TOPLEFT:
                    CGxDevice::ClampRectTop(rect);
                    CGxDevice::ClampRectLeft(rect);
                    CGxDevice::ClampRectTopByAspect(rect);
                    CGxDevice::ClampRectLeftByAspect(rect);
                    break;

                case WMSZ_TOPRIGHT:
                    CGxDevice::ClampRectTop(rect);
                    CGxDevice::ClampRectRight(rect);
                    CGxDevice::ClampRectTopByAspect(rect);
                    CGxDevice::ClampRectRightByAspect(rect);
                    break;

                case WMSZ_BOTTOM:
                    CGxDevice::ClampRectBottom(rect);
                    CGxDevice::ClampRectRightByAspect(rect);
                    break;

                case WMSZ_BOTTOMLEFT:
                    CGxDevice::ClampRectBottom(rect);
                    CGxDevice::ClampRectLeft(rect);
                    CGxDevice::ClampRectLeftByAspect(rect);
                    CGxDevice::ClampRectBottomByAspect(rect);
                    break;

                case WMSZ_BOTTOMRIGHT:
                    CGxDevice::ClampRectBottom(rect);
                    CGxDevice::ClampRectRight(rect);
                    CGxDevice::ClampRectBottomByAspect(rect);
                    CGxDevice::ClampRectRightByAspect(rect);
                    break;

                default:
                    break;
            }

            sizingRect->left = rect.minX;
            sizingRect->bottom = rect.maxY;
            sizingRect->top = rect.minY;
            sizingRect->right = rect.maxX;

            return 1;
        }

        default:
            break;
    }

    if (device && device->m_windowProc) {
        return device->m_windowProc(hwnd, message, wparam, lparam);
    }

    return DefWindowProcA(hwnd, message, wparam, lparam);
}

void CGxDeviceD3d11::ISetWindowFocus(int32_t focus) {
    this->m_windowFocus = focus;

    if (focus && !this->m_format.m_window) {
        RECT rect;
        GetWindowRect(this->m_hwnd, &rect);
        ClipCursor(&rect);
    }
}

void* CGxDeviceD3d11::DeviceWindow() {
    return this->m_hwnd;
}

void CGxDeviceD3d11::CapsWindowSize(CRect& rect) {
    rect = this->DeviceCurWindow();
}

void CGxDeviceD3d11::CapsWindowSizeInScreenCoords(CRect& rect) {
    if (this->IDevIsWindowed()) {
        const CRect& window = this->DeviceCurWindow();
        POINT points[2];
        points[0].x = 0;
        points[0].y = 0;
        points[1].x = static_cast<LONG>(window.maxX);
        points[1].y = static_cast<LONG>(window.maxY);
        MapWindowPoints(this->m_hwnd, nullptr, points, 2);
        rect.minY = static_cast<float>(points[0].y);
        rect.minX = static_cast<float>(points[0].x);
        rect.maxY = static_cast<float>(points[1].y);
        rect.maxX = static_cast<float>(points[1].x);
    } else {
        rect = this->DeviceCurWindow();
    }
}

// ---------------------------------------------------------------------------------------------
// creation / destruction

int32_t CGxDeviceD3d11::DeviceCreate(GxWindowProc windowProc, const CGxFormat& format) {
    this->m_ownhwnd = 1;

    HDC dc = GetDC(nullptr);

    if (GetDeviceGammaRamp(dc, &this->m_systemGammaRamp) && &this->m_gammaRamp != &this->m_systemGammaRamp) {
        this->m_gammaRamp = this->m_systemGammaRamp;
    }

    ReleaseDC(nullptr, dc);

    this->m_hwndClass = WindowClassCreate();

    if (!this->m_hwndClass) {
        char* error = OsGetLastErrorStr();
        CGxDevice::Log("CGxDeviceD3d11::DeviceCreate(): WindowClassCreate() failed: %s", error);
        OsFreeLastErrorStr(error);
        this->DeviceDestroy();
        return 0;
    }

    if (CGxDevice::DeviceCreate(windowProc, format)) {
        return 1;
    }

    this->DeviceDestroy();
    return 0;
}

int32_t CGxDeviceD3d11::DeviceCreate(void* window, const CGxFormat& format) {
    this->m_ownhwnd = 0;
    CGxDevice::DeviceCreate(window, format);
    this->m_hwnd = static_cast<HWND>(window);

    if (this->ICreateD3dDevice(format) && CGxDevice::DeviceCreate(window, format)) {
        return 1;
    }

    this->DeviceDestroy();
    return 0;
}

int32_t CGxDeviceD3d11::DeviceSetFormat(const CGxFormat& format) {
    CGxDevice::Log("CGxDeviceD3d11::DeviceSetFormat():");
    this->Log(format);

    if (this->m_hwnd) {
        ShowWindow(this->m_hwnd, SW_HIDE);
    }

    this->IDestroyD3d();

    if (this->m_hwnd) {
        DestroyWindow(this->m_hwnd);
    }

    this->m_hwnd = nullptr;
    this->m_format = format;

    CGxFormat createFormat = format;

    if (this->ICreateWindow(createFormat) && this->ICreateD3dDevice(createFormat) && CGxDevice::DeviceSetFormat(createFormat)) {
        this->ISetWindowFocus(1);
        return 1;
    }

    CGxDevice::Log("CGxDeviceD3d11::DeviceSetFormat(): unable to set format!");
    this->IDestroyD3d();

    if (this->m_hwnd) {
        DestroyWindow(this->m_hwnd);
    }

    this->m_hwnd = nullptr;

    return 0;
}

void CGxDeviceD3d11::DeviceDestroy() {
    CGxDevice::DeviceDestroy();

    if (this->m_hwnd && this->m_ownhwnd) {
        DestroyWindow(this->m_hwnd);
        this->m_hwnd = nullptr;
    }

    if (this->m_hwndClass) {
        UnregisterClassA(reinterpret_cast<const char*>(static_cast<uintptr_t>(this->m_hwndClass)), GetModuleHandleA(nullptr));
        this->m_hwndClass = 0;
    }

    this->IDestroyD3d();
}

static void SafeRelease(IUnknown*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

template <class T>
static void Rel(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

void CGxDeviceD3d11::IReleaseTargets() {
    if (this->m_d3dContext) {
        // only the output bindings: the constant buffer / shader bindings made at creation must survive a resize
        this->m_d3dContext->OMSetRenderTargets(0, nullptr, nullptr);
    }

    Rel(this->m_backBufferSrv);
    Rel(this->m_backBufferRtv);
    Rel(this->m_backBuffer);
    Rel(this->m_msaaColorRtv);
    Rel(this->m_msaaColor);
    Rel(this->m_defDepth.m_dsv);
    Rel(this->m_defDepth.m_texture);
    Rel(this->m_scratchDepth.m_dsv);
    Rel(this->m_scratchDepth.m_texture);
    this->m_scratchDepth.m_width = 0;
    this->m_scratchDepth.m_height = 0;

    this->m_curRtv = nullptr;
    this->m_curDsv = nullptr;
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
}

int32_t CGxDeviceD3d11::ICreateTargets() {
    if (!this->m_swapChain) {
        return 0;
    }

    if (FAILED(this->m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&this->m_backBuffer)))) {
        return 0;
    }

    D3D11_TEXTURE2D_DESC desc;
    this->m_backBuffer->GetDesc(&desc);
    this->m_backBufferWidth = desc.Width;
    this->m_backBufferHeight = desc.Height;

    if (FAILED(this->m_d3dDevice->CreateRenderTargetView(this->m_backBuffer, nullptr, &this->m_backBufferRtv))) {
        return 0;
    }

    if (this->m_sampleCount > 1) {
        D3D11_TEXTURE2D_DESC msaa = desc;
        msaa.SampleDesc.Count = this->m_sampleCount;
        msaa.SampleDesc.Quality = this->m_sampleQuality;
        msaa.BindFlags = D3D11_BIND_RENDER_TARGET;
        msaa.MiscFlags = 0;

        if (FAILED(this->m_d3dDevice->CreateTexture2D(&msaa, nullptr, &this->m_msaaColor)) || FAILED(this->m_d3dDevice->CreateRenderTargetView(this->m_msaaColor, nullptr, &this->m_msaaColorRtv))) {
            return 0;
        }
    }

    D3D11_TEXTURE2D_DESC depth = {};
    depth.Width = desc.Width;
    depth.Height = desc.Height;
    depth.MipLevels = 1;
    depth.ArraySize = 1;
    depth.Format = this->m_depthFormat;
    depth.SampleDesc.Count = this->m_sampleCount;
    depth.SampleDesc.Quality = this->m_sampleQuality;
    depth.Usage = D3D11_USAGE_DEFAULT;
    depth.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    if (FAILED(this->m_d3dDevice->CreateTexture2D(&depth, nullptr, &this->m_defDepth.m_texture)) || FAILED(this->m_d3dDevice->CreateDepthStencilView(this->m_defDepth.m_texture, nullptr, &this->m_defDepth.m_dsv))) {
        return 0;
    }

    this->m_defDepth.m_width = desc.Width;
    this->m_defDepth.m_height = desc.Height;
    this->m_targetsDirty = 1;
    return 1;
}

int32_t CGxDeviceD3d11::ICreateSwapChain(const CGxFormat& format) {
    RECT client;
    GetClientRect(this->m_hwnd, &client);

    uint32_t width = client.right - client.left;
    uint32_t height = client.bottom - client.top;

    if (!width || !height) {
        width = format.m_size.x ? format.m_size.x : 1;
        height = format.m_size.y ? format.m_size.y : 1;
    }

    this->m_sampleCount = 1;
    this->m_sampleQuality = 0;

    if (format.m_multisampleCount > 1) {
        UINT levels = 0;

        if (SUCCEEDED(this->m_d3dDevice->CheckMultisampleQualityLevels(this->m_backBufferFormat, format.m_multisampleCount, &levels)) && levels > 0) {
            this->m_sampleCount = format.m_multisampleCount;
            this->m_sampleQuality = 0;
        }
    }

    switch (format.m_depthFormat) {
        case CGxFormat::Fmt_Ds160:
            this->m_depthFormat = DXGI_FORMAT_D16_UNORM;
            break;

        case CGxFormat::Fmt_Ds320:
            this->m_depthFormat = DXGI_FORMAT_D32_FLOAT;
            break;

        default:
            this->m_depthFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
            break;
    }

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferDesc.Width = width;
    sd.BufferDesc.Height = height;
    sd.BufferDesc.Format = this->m_backBufferFormat;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
    sd.BufferCount = 1;
    sd.OutputWindow = this->m_hwnd;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    if (FAILED(this->m_dxgiFactory->CreateSwapChain(this->m_d3dDevice, &sd, &this->m_swapChain))) {
        CGxDevice::Log("CGxDeviceD3d11::ICreateSwapChain(): CreateSwapChain failed");
        return 0;
    }

    this->m_dxgiFactory->MakeWindowAssociation(this->m_hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    return this->ICreateTargets();
}

int32_t CGxDeviceD3d11::ICreateD3dDevice(const CGxFormat& format) {
    s_d3d11Debug = GetEnvironmentVariableA("WOW_D3D11_DEBUG", nullptr, 0) != 0;

    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&this->m_dxgiFactory)))) {
        CGxDevice::Log("CGxDeviceD3d11::ICreateD3dDevice(): CreateDXGIFactory1 failed");
        return 0;
    }

    IDXGIAdapter1* adapter = nullptr;
    this->m_dxgiFactory->EnumAdapters1(0, &adapter);

    static const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    if (s_d3d11Debug) {
        flags |= D3D11_CREATE_DEVICE_DEBUG;
    }

    HRESULT hr = D3D11CreateDevice(adapter, adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, sizeof(levels) / sizeof(levels[0]), D3D11_SDK_VERSION, &this->m_d3dDevice, &this->m_featureLevel, &this->m_d3dContext);

    if (FAILED(hr) && hr == E_INVALIDARG) {
        // 11_1 is unknown to older runtimes
        hr = D3D11CreateDevice(adapter, adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels + 1, sizeof(levels) / sizeof(levels[0]) - 1, D3D11_SDK_VERSION, &this->m_d3dDevice, &this->m_featureLevel, &this->m_d3dContext);
    }

    if (adapter) {
        adapter->Release();
    }

    if (FAILED(hr)) {
        CGxDevice::Log("CGxDeviceD3d11::ICreateD3dDevice(): D3D11CreateDevice failed 0x%08X", hr);
        return 0;
    }

    if (!this->ICreateSwapChain(format)) {
        return 0;
    }

    this->m_context = 1;
    this->ISetCaps(format);
    this->Log(this->m_caps);
    CGxDevice::Log("\tD3D11 feature level: 0x%04X", this->m_featureLevel);
    D3d11Log("device created: feature level 0x%04X, backbuffer %ux%u, msaa x%u, window %d", this->m_featureLevel, this->m_backBufferWidth, this->m_backBufferHeight, this->m_sampleCount, this->m_format.m_window);

    // constants
    this->m_constants[0] = this->ICreateBuffer(sizeof(ShadowConstants::constants), D3D11_BIND_CONSTANT_BUFFER);
    this->m_constants[1] = this->ICreateBuffer(sizeof(ShadowConstants::constants), D3D11_BIND_CONSTANT_BUFFER);
    this->m_extraConstants[0] = this->ICreateBuffer(sizeof(this->m_extra[0]), D3D11_BIND_CONSTANT_BUFFER);
    this->m_extraConstants[1] = this->ICreateBuffer(sizeof(this->m_extra[1]), D3D11_BIND_CONSTANT_BUFFER);
    this->m_extraDirty[0] = 1;
    this->m_extraDirty[1] = 1;

    // vertex data for inputs a vertex format does not provide
    float zero[16] = {};
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(zero);
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sr = { zero, 0, 0 };
    this->m_d3dDevice->CreateBuffer(&bd, &sr, &this->m_nullVertexBuffer);

    if (!this->ICreateFixedFunction()) {
        CGxDevice::Log("CGxDeviceD3d11::ICreateD3dDevice(): fixed function shaders failed to compile");
        return 0;
    }

    this->IRsForceUpdate();
    this->IRsSync(0);
    this->m_primVertexDirty = 0xFFFFFFFF;
    this->m_primIndexDirty = 0;
    this->InitLights();

    this->DeviceSetGamma(this->m_gammaRamp);
    this->ICursorCreate(format);

    static uint32_t s_defaultColor = 0xFF00FF00;

    GxTexCreate(8, 8, GxTex_Argb8888, CGxTexFlags(GxTex_Linear), reinterpret_cast<void*>(static_cast<uintptr_t>(s_defaultColor)), [](EGxTexCommand command, uint32_t width, uint32_t height, uint32_t face, uint32_t level, void* userArg, uint32_t& texelStrideInBytes, const void*& texels) {
        static CImVector s_texels[64];

        switch (command) {
            case GxTex_Lock: {
                uint32_t color = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(userArg));

                for (uint32_t i = 0; i < 64; i++) {
                    *reinterpret_cast<uint32_t*>(&s_texels[i]) = color;
                }

                break;
            }

            case GxTex_Latch:
                texelStrideInBytes = 4 * width;
                texels = s_texels;
                break;

            default:
                break;
        }
    }, this->m_defaultGxTex);

    if (this->m_defaultGxTex) {
        GxTexUpdate(this->m_defaultGxTex, 0, 0, 0, 0, 1);
    }

    g_theGxDevicePtr->ShaderCreate(CGxDevice::s_uiVertexShader, GxSh_Vertex, "Shaders\\Vertex", "UI", 2);
    g_theGxDevicePtr->ShaderCreate(&CGxDevice::s_uiPixelShader, GxSh_Pixel, "Shaders\\Pixel", "UI", 1);

    return 1;
}

void CGxDeviceD3d11::IDestroyD3d() {
    for (uint32_t i = 0; i < 2; i++) {
        if (CGxDevice::s_uiVertexShader[i]) {
            g_theGxDevicePtr->ShaderDestroy(CGxDevice::s_uiVertexShader[i]);
        }
    }

    if (CGxDevice::s_uiPixelShader) {
        g_theGxDevicePtr->ShaderDestroy(CGxDevice::s_uiPixelShader);
    }

    this->ICursorDestroy();

    if (this->m_defaultGxTex) {
        GxTexDestroy(this->m_defaultGxTex);
        this->m_defaultGxTex = nullptr;
    }

    this->IReleaseResources(1);
    this->IDestroyFixedFunction();
    this->IReleaseTargets();

    if (this->m_d3dContext) {
        this->m_d3dContext->ClearState();
        this->m_d3dContext->Flush();
    }

    Rel(this->m_nullVertexBuffer);
    Rel(this->m_fanIndexBuffer);
    this->m_fanIndexBufferSize = 0;

    for (uint32_t i = 0; i < 2; i++) {
        Rel(this->m_constants[i]);
        Rel(this->m_extraConstants[i]);
    }

    this->IReleaseStateCaches();

    if (this->m_swapChain) {
        this->m_swapChain->SetFullscreenState(FALSE, nullptr);
    }

    Rel(this->m_swapChain);
    Rel(this->m_d3dContext);
    Rel(this->m_d3dDevice);
    Rel(this->m_dxgiFactory);
    this->m_context = 0;
}

// ---------------------------------------------------------------------------------------------
// caps

void CGxDeviceD3d11::ISetCaps(const CGxFormat& format) {
    CGxCaps& caps = this->m_caps;
    bool fl11 = this->m_featureLevel >= D3D_FEATURE_LEVEL_11_0;
    uint32_t maxTex = fl11 ? 16384 : 8192;

    caps.m_numTmus = 8;
    // D3D10+ rasterization matches OpenGL: pixel and texel centres are at +0.5, so no half-pixel shift is wanted
    caps.m_pixelCenterOnEdge = 1;
    caps.m_texelCenterOnEdge = 1;
    caps.m_texMaxSize[GxTex_2d] = maxTex;
    caps.m_texMaxSize[GxTex_CubeMap] = maxTex;
    caps.m_texMaxSize[GxTex_Rectangle] = maxTex;
    caps.m_texMaxSize[GxTex_NonPow2] = maxTex;
    caps.m_maxIndex = 0xFFFFFF;
    caps.m_texFilterTrilinear = 1;
    caps.m_texFilterAnisotropic = 1;
    caps.m_maxTexAnisotropy = 16;
    caps.m_depthBias = 1;
    caps.m_numStreams = 16;
    caps.m_streamOffset = 1;  // vertex stream offsets supported

    for (uint32_t i = 0; i < GxShTargets_Last; i++) {
        caps.m_shaderTargets[i] = 0;
        caps.m_shaderConstants[i] = 0;
    }

    caps.m_shaderTargets[GxSh_Pixel] = 4;
    caps.m_shaderTargets[GxSh_Vertex] = 3;
    caps.m_shaderConstants[GxSh_Vertex] = 256;
    caps.m_shaderConstants[GxSh_Pixel] = 256;

    if (format.m_fixedFunction[GxSh_Pixel] != -1 && format.m_fixedFunction[GxSh_Pixel] <= caps.m_shaderTargets[GxSh_Pixel]) {
        caps.m_shaderTargets[GxSh_Pixel] = format.m_fixedFunction[GxSh_Pixel];
    }

    if (format.m_fixedFunction[GxSh_Vertex] != -1 && format.m_fixedFunction[GxSh_Vertex] <= caps.m_shaderTargets[GxSh_Vertex]) {
        caps.m_shaderTargets[GxSh_Vertex] = format.m_fixedFunction[GxSh_Vertex];
    }

    if (caps.m_shaderTargets[GxSh_Pixel] == 4 && caps.m_shaderTargets[GxSh_Vertex] != 3) {
        caps.m_shaderTargets[GxSh_Pixel] = 3;
    }

    for (uint32_t i = 0; i < GxTexFormats_Last; i++) {
        UINT support = 0;

        if (i && i != GxTex_D24X8) {
            this->m_d3dDevice->CheckFormatSupport(s_texFormatToDxgi[i], &support);
        } else if (i == GxTex_D24X8) {
            this->m_d3dDevice->CheckFormatSupport(DXGI_FORMAT_R24_UNORM_X8_TYPELESS, &support);
        }

        caps.m_texFmt[i] = (support & D3D11_FORMAT_SUPPORT_TEXTURE2D) != 0;

        // formats the GPU lacks are stored as 8888 (the Blit path converts)
        s_texFormatToUse[i] = static_cast<EGxTexFormat>(i);

        if (i && !caps.m_texFmt[i] && i != GxTex_D24X8) {
            s_texFormatToUse[i] = GxTex_Argb8888;
        }
    }

    auto rtSupport = [this](DXGI_FORMAT fmt, UINT bit) {
        UINT support = 0;
        this->m_d3dDevice->CheckFormatSupport(fmt, &support);
        return (support & bit) != 0;
    };

    caps.m_rttFormat[GxTex_Argb8888] = rtSupport(DXGI_FORMAT_B8G8R8A8_UNORM, D3D11_FORMAT_SUPPORT_RENDER_TARGET);
    caps.m_rttFormat[GxTex_Rgb565] = rtSupport(DXGI_FORMAT_B5G6R5_UNORM, D3D11_FORMAT_SUPPORT_RENDER_TARGET);
    caps.m_rttFormat[GxTex_Gr1616F] = rtSupport(DXGI_FORMAT_R16G16_FLOAT, D3D11_FORMAT_SUPPORT_RENDER_TARGET);
    caps.m_rttFormat[GxTex_R32F] = rtSupport(DXGI_FORMAT_R32_FLOAT, D3D11_FORMAT_SUPPORT_RENDER_TARGET);
    caps.m_rttFormat[GxTex_D24X8] = rtSupport(DXGI_FORMAT_D24_UNORM_S8_UINT, D3D11_FORMAT_SUPPORT_DEPTH_STENCIL);

    caps.m_generateMipMaps = 0;
    caps.m_colorWrite = 1;
    caps.m_hwCursor = 0;
    caps.m_maxClipPlanes = 6;
    caps.m_texTarget[GxTex_2d] = 1;
    caps.m_texTarget[GxTex_CubeMap] = 1;
    caps.m_texTarget[GxTex_Rectangle] = 0;
    caps.m_texTarget[GxTex_NonPow2] = 1;
    caps.m_texNonPow2 = 1;
    caps.m_occlusionQuery = 1;
    caps.m_pointParameters = 0;
    caps.m_pointScaleMax = 1.0f;
    caps.m_pointSprite = 0;
    caps.m_blendFactor = 1;
    caps.m_shaderAlphaRef = 1;

    for (uint32_t i = 0; i < 5; i++) {
        caps.unused_0114[i] = 0;
    }

    caps.unk_0134 = caps.m_shaderTargets[GxSh_Pixel] != 4;
    caps.unk_0138 = caps.m_shaderTargets[GxSh_Pixel] != 4;
}

void CGxDeviceD3d11::DeviceOverride(EGxOverride override, uint32_t value) {
    CGxDevice::DeviceOverride(override, value);

    if (override == GxOverride_PixelShader) {
        this->m_caps.m_shaderTargets[GxSh_Pixel] = value;
    } else if (override == GxOverride_Unk7) {
        this->m_caps.m_texNonPow2 = value != 0;
    }
}

// ---------------------------------------------------------------------------------------------
// window messages

void CGxDeviceD3d11::IResize() {
    if (!this->m_swapChain) {
        return;
    }

    RECT client;
    GetClientRect(this->m_hwnd, &client);
    uint32_t width = client.right - client.left;
    uint32_t height = client.bottom - client.top;

    if (!width || !height || (width == this->m_backBufferWidth && height == this->m_backBufferHeight)) {
        return;
    }

    this->IReleaseTargets();

    if (FAILED(this->m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0)) || !this->ICreateTargets()) {
        CGxDevice::Log("CGxDeviceD3d11::IResize(): failed to resize swap chain");
        this->m_context = 0;
    }
}

int32_t CGxDeviceD3d11::DeviceWM(EGxWM wm, uintptr_t param1, uintptr_t param2) {
    switch (wm) {
        case GxWM_Size:
            if (param2 == 1 || param2 == 2) {
                this->m_windowVisible = 0;
                break;
            }

            this->m_windowVisible = 1;
            this->DeviceSetDefWindow(*reinterpret_cast<const CRect*>(param1));

            if (this->m_d3dDevice && this->m_context) {
                this->IResize();

                if (this->m_context) {
                    this->ISetWindowFocus(1);
                    this->m_intF5C = 0;
                    this->NotifyOnDeviceRestored();
                }
            }

            this->m_viewportDirty = 1;
            break;

        case GxWM_DisplayChange:
            if (this->m_windowVisible) {
                this->DeviceSetDefWindow(*reinterpret_cast<const CRect*>(param1));
                this->m_viewportDirty = 1;
            }

            break;

        case GxWM_Destroy:
        case GxWM_KillFocus:
            this->m_windowFocus = 0;
            break;

        case GxWM_SetFocus:
            this->ISetWindowFocus(1);
            break;

        default:
            break;
    }

    return 0;
}

// ---------------------------------------------------------------------------------------------
// scene

void CGxDeviceD3d11::ScenePresent() {
    if (this->m_context) {
        int32_t scrShotClick = this->m_scrShotClick;

        CGxDevice::ScenePresent();
        this->ICursorDraw();

        if (scrShotClick) {
            this->DeviceScreenShot();
        }

        this->WaitForFPSCap();

        if (this->m_msaaColor) {
            this->m_d3dContext->ResolveSubresource(this->m_backBuffer, 0, this->m_msaaColor, 0, this->m_backBufferFormat);
        }

        UINT interval = this->m_format.m_vsync ? 1 : 0;
        HRESULT hr = this->m_swapChain->Present(interval, 0);

        this->m_statFrames++;

        if (this->m_statFrames <= 3 || this->m_statFrames % 300 == 0) {
            D3d11Log("frame %u: draws %u (fixed function %u, skipped %u), present hr=0x%08X", this->m_statFrames, this->m_statDraws, this->m_statFfDraws, this->m_statSkipped, hr);
        }

if (this->m_statFrames == 300 || this->m_statFrames == 900) {
            this->m_logDraws = 12;
        }

        static bool s_worldTrace = false;

        if (!s_worldTrace && this->m_statDraws > 400) {
            s_worldTrace = true;
            this->m_logDraws = 2000;
        }

        if (s_worldTrace && this->m_statFrames % 600 == 0 && this->m_statDraws > 400) {
            this->m_logDraws = 20;
        }

        this->m_statDraws = 0;
        this->m_statFfDraws = 0;
        this->m_statSkipped = 0;
        this->ShaderConstantsClear();

        if (FAILED(hr) && hr != DXGI_ERROR_WAS_STILL_DRAWING) {
            if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
                HRESULT reason = this->m_d3dDevice->GetDeviceRemovedReason();
                CGxDevice::Log("CGxDeviceD3d11::ScenePresent(): device removed, reason 0x%08X", reason);
                this->m_context = 0;
            }
        }
    }
}

void CGxDeviceD3d11::SceneClear(uint32_t mask, CImVector color) {
    if (!this->m_context) {
        return;
    }

    this->ISetTargets();

    if (this->m_statFrames < 3) {
        D3d11Log("clear mask %u color %u,%u,%u,%u after %u draws | viewport norm x %g-%g y %g-%g z %g-%g | cur window %g x %g", mask, color.r, color.g, color.b, color.a, this->m_statDraws, this->m_viewport.m_x.m_l, this->m_viewport.m_x.m_h, this->m_viewport.m_y.m_l, this->m_viewport.m_y.m_h, this->m_viewport.m_z.m_l, this->m_viewport.m_z.m_h, this->DeviceCurWindow().maxX, this->DeviceCurWindow().maxY);
    }

    if ((mask & 0x1) && this->m_curRtv) {
        float rgba[4] = { color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f };

        static const bool s_clearBlue = GetEnvironmentVariableA("WOW_D3D11_CLEARBLUE", nullptr, 0) != 0;

        if (s_clearBlue) {
            rgba[0] = 0.0f;
            rgba[1] = 0.3f;
            rgba[2] = 1.0f;
            rgba[3] = 1.0f;
        }
        this->m_d3dContext->ClearRenderTargetView(this->m_curRtv, rgba);
    }

    if ((mask & 0x2) && this->m_curDsv) {
        this->m_d3dContext->ClearDepthStencilView(this->m_curDsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    }
}

CGxDeviceD3d11::DepthTarget* CGxDeviceD3d11::IGetScratchDepth(uint32_t width, uint32_t height) {
    DepthTarget& d = this->m_scratchDepth;

    if (d.m_dsv && d.m_width >= width && d.m_height >= height) {
        return &d;
    }

    Rel(d.m_dsv);
    Rel(d.m_texture);

    if (width < d.m_width) {
        width = d.m_width;
    }

    if (height < d.m_height) {
        height = d.m_height;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    if (FAILED(this->m_d3dDevice->CreateTexture2D(&desc, nullptr, &d.m_texture)) || FAILED(this->m_d3dDevice->CreateDepthStencilView(d.m_texture, nullptr, &d.m_dsv))) {
        Rel(d.m_dsv);
        Rel(d.m_texture);
        return nullptr;
    }

    d.m_width = width;
    d.m_height = height;
    return &d;
}

// Chooses and binds the current colour / depth views from the Gx render target state.
void CGxDeviceD3d11::ISetTargets() {
    CGxDevice::TextureTarget& colorTarget = this->m_textureTarget[GxBuffers_Color];
    CGxDevice::TextureTarget& depthTarget = this->m_textureTarget[GxBuffers_Depth];

    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;

    TexData* colorData = colorTarget.m_texture ? static_cast<TexData*>(colorTarget.m_apiSpecific) : nullptr;
    TexData* depthData = depthTarget.m_texture ? static_cast<TexData*>(depthTarget.m_apiSpecific) : nullptr;

    if (colorData) {
        uint32_t face = colorTarget.m_texture->m_target == GxTex_CubeMap ? colorTarget.m_plane : 0;
        rtv = colorData->m_rtv[face < 6 ? face : 0];
    } else if (!depthData) {
        rtv = this->m_msaaColorRtv ? this->m_msaaColorRtv : this->m_backBufferRtv;
    }

    if (depthData) {
        dsv = depthData->m_dsv;
    } else if (this->m_depthOverride) {
        dsv = this->m_depthOverride->m_dsv;
    } else if (colorData) {
        DepthTarget* scratch = this->IGetScratchDepth(colorData->m_width, colorData->m_height);
        dsv = scratch ? scratch->m_dsv : nullptr;
    } else {
        dsv = this->m_defDepth.m_dsv;
    }

    if (this->m_targetsDirty || rtv != this->m_curRtv || dsv != this->m_curDsv) {
        // a texture cannot be sampled while it is bound for output
        for (uint32_t i = 0; i < 16; i++) {
            if (this->m_curSrvs[i] && ((colorData && this->m_curSrvs[i] == colorData->m_srv) || (depthData && this->m_curSrvs[i] == depthData->m_srv))) {
                ID3D11ShaderResourceView* null = nullptr;
                this->m_d3dContext->PSSetShaderResources(i, 1, &null);
                this->m_curSrvs[i] = nullptr;
            }
        }

        this->m_d3dContext->OMSetRenderTargets(rtv ? 1 : 0, &rtv, dsv);
        this->m_curRtv = rtv;
        this->m_curDsv = dsv;
        this->m_targetsDirty = 0;
        this->m_viewportDirty = 1;
        this->m_scissorRectDirty = 1;
    }
}

void CGxDeviceD3d11::DeviceSetRenderTarget(EGxBuffer buffer, CGxTex* texture, uint32_t plane) {
    if (!this->m_context) {
        return;
    }

    CGxDevice::TextureTarget& target = this->m_textureTarget[buffer];

    if (target.m_texture == texture && target.m_plane == plane) {
        return;
    }

    CGxDevice::DeviceSetRenderTarget(buffer, texture, plane);

    static int32_t s_rtLogs = 0;

    if (s_rtLogs++ < 40) {
        D3d11Log("SetRenderTarget buffer %d tex %p %ux%u fmt %d rt %d plane %u", buffer, texture, texture ? texture->m_width : 0, texture ? texture->m_height : 0, texture ? texture->m_format : 0, texture ? texture->m_flags.m_renderTarget : 0, plane);
    }

    target.m_apiSpecific = nullptr;

    if (texture) {
        target.m_apiSpecific = this->ITexEnsure(texture);
    }

    this->m_targetsDirty = 1;

    GxXformSetViewport(this->m_viewport.m_x.m_l, this->m_viewport.m_x.m_h, this->m_viewport.m_y.m_l, this->m_viewport.m_y.m_h, this->m_viewport.m_z.m_l, this->m_viewport.m_z.m_h);

    this->m_viewportDirty = 1;
    this->m_scissorRectDirty = 1;
}

void CGxDeviceD3d11::DeviceSaveDepthSurface() {
}

void CGxDeviceD3d11::DeviceRestoreDepthSurface() {
    this->m_depthOverride = nullptr;
    this->m_targetsDirty = 1;
}

void CGxDeviceD3d11::DeviceResolveDepthBuffer(CGxTex* texture) {
    if (!this->m_context) {
        return;
    }

    this->m_depthOverride = texture ? this->ITexEnsure(texture) : nullptr;
    this->m_targetsDirty = 1;
}

// ---------------------------------------------------------------------------------------------
// copies

// The default colour buffer, resolved if multisampled
ID3D11Texture2D* CGxDeviceD3d11::IResolvedBackBuffer() {
    if (this->m_msaaColor) {
        this->m_d3dContext->ResolveSubresource(this->m_backBuffer, 0, this->m_msaaColor, 0, this->m_backBufferFormat);
    }

    return this->m_backBuffer;
}

void CGxDeviceD3d11::DeviceCopyTex(CGxTex* source, uint32_t sourcePlane, CGxTex* dest, uint32_t destPlane) {
    TexData* src = source ? this->ITexEnsure(source) : nullptr;
    TexData* dst = dest ? this->ITexEnsure(dest) : nullptr;

    if (!src || !dst || !src->m_texture || !dst->m_texture) {
        return;
    }

    if (src->m_format == dst->m_format && src->m_width == dst->m_width && src->m_height == dst->m_height) {
        this->m_d3dContext->CopySubresourceRegion(dst->m_texture, D3D11CalcSubresource(0, destPlane, dst->m_mips), 0, 0, 0, src->m_texture, D3D11CalcSubresource(0, sourcePlane, src->m_mips), nullptr);
    } else {
        this->IBlit(src->m_srv, src->m_width, src->m_height, nullptr, dst, destPlane, 0, nullptr, true);
    }
}

int32_t CGxDeviceD3d11::TexCopy(CGxTex* dest, const C2iVector& destPos, const C2iVector& sourcePos, const C2iVector& size, uint32_t level, uint32_t plane) {
    if (!this->m_context) {
        return 0;
    }

    static int32_t s_copyLogs = 0;

    if (s_copyLogs++ < 20) {
        D3d11Log("TexCopy dest %p %ux%u fmt %d dst %d,%d src %d,%d size %dx%d level %u plane %u", dest, dest->m_width, dest->m_height, dest->m_format, destPos.x, destPos.y, sourcePos.x, sourcePos.y, size.x, size.y, level, plane);
    }

    TexData* dst = this->ITexEnsure(dest);

    if (!dst || !dst->m_texture) {
        return 0;
    }

    ID3D11Texture2D* back = this->IResolvedBackBuffer();
    UINT dstSub = D3D11CalcSubresource(level, plane, dst->m_mips);

    if (dst->m_format == this->m_backBufferFormat) {
        D3D11_BOX box = { static_cast<UINT>(sourcePos.x), static_cast<UINT>(sourcePos.y), 0, static_cast<UINT>(sourcePos.x + size.x), static_cast<UINT>(sourcePos.y + size.y), 1 };

        if (box.right > this->m_backBufferWidth) {
            box.right = this->m_backBufferWidth;
        }

        if (box.bottom > this->m_backBufferHeight) {
            box.bottom = this->m_backBufferHeight;
        }

        this->m_d3dContext->CopySubresourceRegion(dst->m_texture, dstSub, destPos.x, destPos.y, 0, back, 0, &box);


        return 1;
    }

    RECT srcRect = { sourcePos.x, sourcePos.y, sourcePos.x + size.x, sourcePos.y + size.y };
    RECT dstRect = { destPos.x, destPos.y, destPos.x + size.x, destPos.y + size.y };
    return this->IBlitBackBuffer(dst, plane, level, &srcRect, &dstRect, false);
}

int32_t CGxDeviceD3d11::TexStretch(CGxTex* dest, const CiRect* destRect, const CiRect* sourceRect, uint32_t level, uint32_t face) {
    if (!this->m_context) {
        return 0;
    }

    static int32_t s_stretchLogs = 0;

    if (s_stretchLogs++ < 20) {
        D3d11Log("TexStretch dest %p %ux%u fmt %d rt %d destRect %d,%d-%d,%d srcRect %d,%d-%d,%d level %u face %u", dest, dest->m_width, dest->m_height, dest->m_format, dest->m_flags.m_renderTarget, destRect ? destRect->minX : -1, destRect ? destRect->minY : -1, destRect ? destRect->maxX : -1, destRect ? destRect->maxY : -1, sourceRect ? sourceRect->minX : -1, sourceRect ? sourceRect->minY : -1, sourceRect ? sourceRect->maxX : -1, sourceRect ? sourceRect->maxY : -1, level, face);
    }

    TexData* dst = this->ITexEnsure(dest);

    if (!dst || !dst->m_texture) {
        return 0;
    }

    RECT d3dDest = { 0, 0, 0, 0 };
    RECT d3dSrc = { 0, 0, 0, 0 };

    if (destRect) {
        d3dDest = { destRect->minX, destRect->minY, destRect->maxX, destRect->maxY };
    }

    if (sourceRect) {
        d3dSrc = { sourceRect->minX, sourceRect->minY, sourceRect->maxX, sourceRect->maxY };
    }

    bool linear = !(destRect && sourceRect && d3dDest.right - d3dDest.left == d3dSrc.right - d3dSrc.left && d3dDest.bottom - d3dDest.top == d3dSrc.bottom - d3dSrc.top);

    return this->IBlitBackBuffer(dst, face, level, sourceRect ? &d3dSrc : nullptr, destRect ? &d3dDest : nullptr, linear);
}

int32_t CGxDeviceD3d11::IBlitBackBuffer(TexData* dst, uint32_t face, uint32_t level, const RECT* srcRect, const RECT* dstRect, bool linear) {
    this->IResolvedBackBuffer();

    if (!this->m_backBufferSrv) {
        this->m_d3dDevice->CreateShaderResourceView(this->m_backBuffer, nullptr, &this->m_backBufferSrv);
    }

    if (!this->m_backBufferSrv) {
        return 0;
    }

    return this->IBlit(this->m_backBufferSrv, this->m_backBufferWidth, this->m_backBufferHeight, srcRect, dst, face, level, dstRect, linear);
}

// ---------------------------------------------------------------------------------------------
// read back

void CGxDeviceD3d11::DeviceReadPixels(CiRect& rect, TSGrowableArray<CImVector>& pixels) {
    this->ClampRectToWindow(rect);

    uint32_t height = rect.maxY - rect.minY;
    uint32_t width = rect.maxX - rect.minX;
    uint32_t count = width * height;
    pixels.SetCount(count);
    memset(pixels.Ptr(), 0xFF, sizeof(CImVector) * count);

    if (!this->m_context || !width || !height) {
        return;
    }

    ID3D11Texture2D* back = this->IResolvedBackBuffer();

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = this->m_backBufferFormat;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    ID3D11Texture2D* staging = nullptr;

    if (FAILED(this->m_d3dDevice->CreateTexture2D(&desc, nullptr, &staging))) {
        return;
    }

    D3D11_BOX box = { static_cast<UINT>(rect.minX), static_cast<UINT>(rect.minY), 0, static_cast<UINT>(rect.maxX), static_cast<UINT>(rect.maxY), 1 };
    this->m_d3dContext->CopySubresourceRegion(staging, 0, 0, 0, 0, back, 0, &box);

    D3D11_MAPPED_SUBRESOURCE mapped;

    if (SUCCEEDED(this->m_d3dContext->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
        const uint8_t* source = static_cast<const uint8_t*>(mapped.pData);
        CImVector* dest = pixels.Ptr();

        for (uint32_t y = 0; y < height; y++) {
            memcpy(dest, source, sizeof(CImVector) * width);
            source += mapped.RowPitch;
            dest += width;
        }

        this->m_d3dContext->Unmap(staging, 0);
    }

    staging->Release();
}

void CGxDeviceD3d11::DeviceReadDepths(CiRect& rect, TSGrowableArray<float>& depths) {
    this->ClampRectToWindow(rect);

    uint32_t count = (rect.maxY - rect.minY) * (rect.maxX - rect.minX);
    depths.SetCount(count);
    memset(depths.Ptr(), 0, sizeof(float) * count);
}

// ---------------------------------------------------------------------------------------------
// queries

void CGxDeviceD3d11::QueryCreate(CGxQuery*& query, EGxQueryType type) {
    CGxDevice::QueryCreate(query, type);

    CGxQuery* gxQuery = query;

    if (!gxQuery->m_apiSpecific) {
        D3D11_QUERY_DESC desc = { D3D11_QUERY_OCCLUSION, 0 };
        ID3D11Query* d3dQuery = nullptr;

        if (SUCCEEDED(this->m_d3dDevice->CreateQuery(&desc, &d3dQuery))) {
            gxQuery->m_apiSpecific = d3dQuery;
        }
    }
}

void CGxDeviceD3d11::QueryDestroy(CGxQuery*& query) {
    CGxQuery* gxQuery = query;

    if (gxQuery->m_apiSpecific) {
        static_cast<ID3D11Query*>(gxQuery->m_apiSpecific)->Release();
    }

    gxQuery->m_apiSpecific = nullptr;
    CGxDevice::QueryDestroy(query);
}

static ID3D11Query* EnsureQuery(ID3D11Device* device, CGxQuery* query) {
    if (!query->m_apiSpecific) {
        D3D11_QUERY_DESC desc = { D3D11_QUERY_OCCLUSION, 0 };
        ID3D11Query* d3dQuery = nullptr;

        if (SUCCEEDED(device->CreateQuery(&desc, &d3dQuery))) {
            query->m_apiSpecific = d3dQuery;
        }
    }

    return static_cast<ID3D11Query*>(query->m_apiSpecific);
}

bool CGxDeviceD3d11::QueryBegin(CGxQuery* query) {
    if (!this->m_context) {
        return false;
    }

    ID3D11Query* q = EnsureQuery(this->m_d3dDevice, query);

    if (!q) {
        return false;
    }

    this->m_d3dContext->Begin(q);
    return true;
}

bool CGxDeviceD3d11::QueryEnd(CGxQuery* query) {
    if (!this->m_context || !query->m_apiSpecific) {
        return false;
    }

    this->m_d3dContext->End(static_cast<ID3D11Query*>(query->m_apiSpecific));
    return true;
}

bool CGxDeviceD3d11::QueryGetParam(CGxQuery* query, EGxQueryParam param, uint32_t& data) {
    if (!this->m_context) {
        return false;
    }

    ID3D11Query* q = EnsureQuery(this->m_d3dDevice, query);

    if (!q) {
        return false;
    }

    switch (param) {
        case GxQueryParam_Unk0:
            data = sizeof(uint32_t);
            return true;

        case GxQueryParam_Unk1: {
            UINT64 samples;
            data = this->m_d3dContext->GetData(q, &samples, sizeof(samples), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
            return false;
        }

        default:
            return false;
    }
}

bool CGxDeviceD3d11::QueryGetData(CGxQuery* query, uint32_t& data) {
    if (!this->m_context || !query->m_apiSpecific) {
        return false;
    }

    ID3D11Query* q = static_cast<ID3D11Query*>(query->m_apiSpecific);
    UINT64 samples = 0;
    HRESULT hr;

    for (uint32_t spins = 0; spins < 1000000; spins++) {
        hr = this->m_d3dContext->GetData(q, &samples, sizeof(samples), 0);

        if (hr != S_FALSE) {
            break;
        }
    }

    if (hr != S_OK) {
        return false;
    }

    data = samples > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(samples);
    return true;
}

// ---------------------------------------------------------------------------------------------
// misc

void CGxDeviceD3d11::DeviceEvictResources() {
}

void CGxDeviceD3d11::DeviceSetBaseMipLevel(uint32_t level) {
    this->ITexForceRecreation(1);
    CGxDevice::DeviceSetBaseMipLevel(level);
}

void CGxDeviceD3d11::DeviceSetGamma(float gamma) {
    CGxDevice::DeviceSetGamma(gamma);
}

void CGxDeviceD3d11::DeviceSetGamma(const CGxGammaRamp& ramp) {
    CGxDevice::DeviceSetGamma(ramp);
}

void CGxDeviceD3d11::ICursorCreate(const CGxFormat& format) {
    CGxDevice::ICursorCreate(format);
}

void CGxDeviceD3d11::ICursorDestroy() {
    CGxDevice::ICursorDestroy();
}

void CGxDeviceD3d11::ICursorDraw() {
    CGxDevice::ICursorDraw();
}

void CGxDeviceD3d11::CursorSetVisible(int32_t visible) {
    CGxDevice::CursorSetVisible(visible);
}

void CGxDeviceD3d11::CursorUnlock(uint32_t x, uint32_t y) {
    CGxDevice::CursorUnlock(x, y);
}

void CGxDeviceD3d11::LogCrashInfo(char* buffer, uint32_t size) {
    if (!this->m_dxgiFactory) {
        return;
    }

    char line[260];

    for (UINT i = 0;; i++) {
        IDXGIAdapter1* adapter = nullptr;

        if (FAILED(this->m_dxgiFactory->EnumAdapters1(i, &adapter))) {
            break;
        }

        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        adapter->Release();

        SStrPrintf(line, sizeof(line), "\r\nAdapter %u:\r\n  Description: %ls\r\n  VendorId: %04X DeviceId: %04X\r\n", i, desc.Description, desc.VendorId, desc.DeviceId);
        SStrPack(buffer, line, size);
    }
}

void CGxDeviceD3d11::StereoSetConvergence(float convergence) {
}

float CGxDeviceD3d11::StereoGetConvergence() {
    return 1.0f;
}

void CGxDeviceD3d11::StereoSetSeparation(float separation) {
}

float CGxDeviceD3d11::StereoGetSeparation() {
    return 0.0f;
}

bool CGxDeviceD3d11::StereoEnabled() {
    return false;
}

void CGxDeviceD3d11::DeviceUnk81(uint32_t a, uint32_t b) {
}

void CGxDeviceD3d11::DeviceUnk82() {
}

void CGxDeviceD3d11::DeviceUnk83(uint32_t a, uint32_t b) {
}

void CGxDeviceD3d11::XformSetProjection(const C44Matrix& matrix) {
    // identical to the D3D9 backend: Gx projections are OpenGL style [-1,1] depth, D3D wants [0,1]
    CGxDevice::XformSetProjection(matrix);

    C44Matrix projNative = matrix;

    if (!(fabs(projNative.c3 - 1.0f) < 0.00000023841858f) && !(fabs(projNative.c3) < 0.00000023841858f)) {
        projNative *= 1.0f / projNative.c3;
    }

    if (projNative.d3 == 0.0f) {
        float minZ = -(projNative.d2 / (projNative.c2 + 1.0f));
        float maxZ = -(projNative.d2 / (projNative.c2 - 1.0f));
        projNative.c2 = maxZ / (maxZ - minZ);
        projNative.d2 = maxZ * minZ / (minZ - maxZ);
    } else {
        float invC2 = 1.0f / projNative.c2;
        float minZ = (-1.0f - projNative.d2) * invC2;
        float maxZ = (1.0f - projNative.d2) * invC2;
        projNative.c2 = 1.0f / (maxZ - minZ);
        projNative.d2 = minZ / (minZ - maxZ);
    }

    if (!(this->m_appMasterEnables & (1 << GxMasterEnable_NormalProjection)) && projNative.d3 != 1.0f) {
        C44Matrix scale;
        scale.a0 = 0.2f;
        scale.b1 = 0.2f;
        scale.c2 = 0.2f;
        projNative = projNative * scale;
    }

    this->m_xforms[GxXform_Projection].m_dirty = 1;
    this->m_projNative = projNative;
}

void CGxDeviceD3d11::XformSetView(const C44Matrix& matrix) {
    CGxDevice::XformSetView(matrix);
}
