#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "Engine/Source/Gx/CGxDeviceD3d11/CGxDeviceD3d11.h"
#include "Engine/Source/Gx/Blit.h"
#include "Engine/Source/Gx/CGxBuf.h"
#include "Engine/Source/Gx/CGxPool.h"
#include "Engine/Source/Gx/CGxTex.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Storm/H/storm.h"

template <class T>
static void Rel(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

// ---------------------------------------------------------------------------------------------
// buffers

ID3D11Buffer* CGxDeviceD3d11::ICreateBuffer(uint32_t size, uint32_t bindFlags) {
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = (size + 15) & ~15u;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = bindFlags;

    ID3D11Buffer* buffer = nullptr;

    if (FAILED(this->m_d3dDevice->CreateBuffer(&desc, nullptr, &buffer))) {
        return nullptr;
    }

    return buffer;
}

void CGxDeviceD3d11::IReleasePool(CGxPool* pool) {
    if (pool->m_usage == GxPoolUsage_Stream) {
        pool->unk_001C = 0;
    }

    pool->Invalidate();

    PoolData* data = static_cast<PoolData*>(pool->m_apiSpecific);

    if (data) {
        Rel(data->m_buffer);
        free(data->m_shadow);
        delete data;
        pool->m_apiSpecific = nullptr;
    }
}

int32_t CGxDeviceD3d11::ICreatePoolApi(CGxPool* pool) {
    if (!this->m_d3dDevice || pool->m_size <= 0) {
        return 0;
    }

    PoolData* data = new PoolData();
    data->m_size = pool->m_size;
    data->m_shadow = static_cast<uint8_t*>(calloc(1, pool->m_size));
    data->m_buffer = this->ICreateBuffer(pool->m_size, pool->m_target == GxPoolTarget_Vertex ? D3D11_BIND_VERTEX_BUFFER : D3D11_BIND_INDEX_BUFFER);

    if (!data->m_shadow || !data->m_buffer) {
        Rel(data->m_buffer);
        free(data->m_shadow);
        delete data;
        return 0;
    }

    pool->m_apiSpecific = data;
    return 1;
}

void CGxDeviceD3d11::PoolSizeSet(CGxPool* pool, uint32_t size) {
    this->IReleasePool(pool);
    pool->m_size = size;
    this->ICreatePoolApi(pool);
}

void CGxDeviceD3d11::PoolDestroy(CGxPool* pool) {
    if (pool) {
        this->IReleasePool(pool);
        CGxDevice::PoolDestroy(pool);
    }
}

char* CGxDeviceD3d11::IBufLock(CGxBuf* buf) {
    CGxPool* pool = buf->m_pool;

    if (!this->m_context) {
        return static_cast<char*>(this->m_emergencyMem[pool->m_target].Lock(buf->m_size));
    }

    if (pool->m_usage == GxPoolUsage_Stream) {
        uint32_t index = (buf->m_itemSize + pool->unk_001C - 1) - (buf->m_itemSize + pool->unk_001C - 1) % buf->m_itemSize;

        if (index + buf->m_size > static_cast<uint32_t>(pool->m_size)) {
            pool->Discard();
            buf->m_index = 0;
            pool->unk_001C = buf->m_size;
        } else {
            buf->m_index = index;
            pool->unk_001C = index + buf->m_size;
        }
    }

    if (!pool->m_apiSpecific) {
        this->ICreatePoolApi(pool);
    }

    PoolData* data = static_cast<PoolData*>(pool->m_apiSpecific);

    if (!data || buf->m_index + buf->m_size > data->m_size) {
        static int32_t s_emergencyLogs = 0;

        if (s_emergencyLogs++ < 20) {
            D3d11Log("CGxDeviceD3d11: emergency buffer: pool '%s' target %d usage %d size %d, buf index %u size %u, api %p", pool->m_name ? pool->m_name : "?", pool->m_target, pool->m_usage, pool->m_size, buf->m_index, buf->m_size, pool->m_apiSpecific);
        }

        return static_cast<char*>(this->m_emergencyMem[pool->m_target].Lock(buf->m_size));
    }

    return reinterpret_cast<char*>(data->m_shadow + buf->m_index);
}

void CGxDeviceD3d11::IBufUnlock(CGxBuf* buf) {
    CGxPool* pool = buf->m_pool;
    EmergencyMem& emergencyMem = this->m_emergencyMem[pool->m_target];

    if (emergencyMem.m_lock) {
        emergencyMem.Unlock();
        buf->unk_001D = 0;
        this->m_intF5C = 1;
        return;
    }

    PoolData* data = static_cast<PoolData*>(pool->m_apiSpecific);

    if (!data || !data->m_buffer || !buf->m_size) {
        buf->unk_001D = data != nullptr;
        return;
    }

    D3D11_BOX box = { buf->m_index, 0, 0, buf->m_index + buf->m_size, 1, 1 };
    this->m_d3dContext->UpdateSubresource(data->m_buffer, 0, &box, data->m_shadow + buf->m_index, 0, 0);
    buf->unk_001D = 1;
}

char* CGxDeviceD3d11::BufLock(CGxBuf* buf) {
    CGxDevice::BufLock(buf);
    return this->IBufLock(buf);
}

int32_t CGxDeviceD3d11::BufUnlock(CGxBuf* buf, uint32_t size) {
    CGxDevice::BufUnlock(buf, size);
    this->IBufUnlock(buf);
    return 1;
}

void CGxDeviceD3d11::BufData(CGxBuf* buf, const void* data, uint32_t size, uint32_t offset) {
    CGxDevice::BufData(buf, data, size, offset);

    char* dst = this->IBufLock(buf);
    memcpy(dst + offset, data, size);
    this->IBufUnlock(buf);
}

// ---------------------------------------------------------------------------------------------
// textures

void CGxDeviceD3d11::ITexReleaseData(CGxTex* texId) {
    TexData* data = static_cast<TexData*>(texId->m_apiSpecificData);

    if (!data) {
        return;
    }

    // make sure the context does not keep the views alive / bound
    if (this->m_d3dContext) {
        for (uint32_t i = 0; i < 16; i++) {
            if (this->m_curSrvs[i] && this->m_curSrvs[i] == data->m_srv) {
                ID3D11ShaderResourceView* null = nullptr;
                this->m_d3dContext->PSSetShaderResources(i, 1, &null);
                this->m_curSrvs[i] = nullptr;
            }
        }

        for (uint32_t i = 0; i < 6; i++) {
            if (data->m_rtv[i] && data->m_rtv[i] == this->m_curRtv) {
                this->m_targetsDirty = 1;
                this->m_curRtv = nullptr;
            }
        }

        if (data->m_dsv && data->m_dsv == this->m_curDsv) {
            this->m_targetsDirty = 1;
            this->m_curDsv = nullptr;
        }
    }

    for (uint32_t i = 0; i < 6; i++) {
        Rel(data->m_rtv[i]);
    }

    Rel(data->m_dsv);
    Rel(data->m_srv);
    Rel(data->m_texture);

    if (this->m_depthOverride == data) {
        this->m_depthOverride = nullptr;
    }

    for (uint32_t i = 0; i < GxBuffers_Last; i++) {
        if (this->m_textureTarget[i].m_apiSpecific == data) {
            this->m_textureTarget[i].m_apiSpecific = nullptr;
        }
    }

    delete data;
    texId->m_apiSpecificData = nullptr;
}

void CGxDeviceD3d11::TexDestroy(CGxTex* texId) {
    for (uint32_t i = 0; i < GxBuffers_Last; i++) {
        if (this->m_textureTarget[i].m_texture == texId) {
            this->m_textureTarget[i].m_texture = nullptr;
            this->m_textureTarget[i].m_apiSpecific = nullptr;
            this->m_targetsDirty = 1;
        }
    }

    this->ITexReleaseData(texId);
    CGxDevice::TexDestroy(texId);
}

CGxDeviceD3d11::TexData* CGxDeviceD3d11::ITexEnsure(CGxTex* texId) {
    if (!texId) {
        return nullptr;
    }

    if (texId->m_needsCreation || !texId->m_apiSpecificData) {
        this->ITexCreate(texId);
    }

    if (texId->m_needsCreation) {
        return nullptr;
    }

    return static_cast<TexData*>(texId->m_apiSpecificData);
}

int32_t CGxDeviceD3d11::ITexCreate(CGxTex* texId) {
    if (!this->m_d3dDevice) {
        return 0;
    }

    uint32_t width;
    uint32_t height;
    uint32_t baseMip;
    uint32_t mipCount;
    this->ITexWHDStartEnd(texId, width, height, baseMip, mipCount);

    texId->m_format = s_texFormatToUse[texId->m_format];

    bool isDepth = texId->m_format == GxTex_D24X8;
    bool isCube = texId->m_target == GxTex_CubeMap;
    uint32_t levels = isDepth ? 1 : (mipCount > baseMip ? mipCount - baseMip : 1);
    uint32_t faces = isCube ? 6 : 1;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = levels;
    desc.ArraySize = faces;
    desc.Format = s_texFormatToDxgi[texId->m_format];
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = isCube ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;

    if (isDepth) {
        desc.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
    } else if (texId->m_flags.m_renderTarget) {
        desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
    }

    if (desc.Format == DXGI_FORMAT_UNKNOWN || !width || !height) {
        return 0;
    }

    TexData* data = new TexData();
    data->m_format = desc.Format;
    data->m_width = width;
    data->m_height = height;
    data->m_mips = levels;
    data->m_faces = faces;

    HRESULT hr = this->m_d3dDevice->CreateTexture2D(&desc, nullptr, &data->m_texture);

    if (FAILED(hr) && !texId->m_flags.m_renderTarget && !isDepth) {
        // the GPU refused the format; fall back to 8888 (Blit converts the data)
        s_texFormatToUse[texId->m_format] = GxTex_Argb8888;
        texId->m_format = GxTex_Argb8888;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        data->m_format = desc.Format;
        hr = this->m_d3dDevice->CreateTexture2D(&desc, nullptr, &data->m_texture);
    }

    if (FAILED(hr)) {
        D3d11Log("CGxDeviceD3d11::ITexCreate(): CreateTexture2D %ux%u fmt %d failed 0x%08X", width, height, texId->m_format, hr);
        delete data;
        return 0;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = isDepth ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : desc.Format;

    if (isCube) {
        srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        srv.TextureCube.MipLevels = levels;
    } else {
        srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = levels;
    }

    this->m_d3dDevice->CreateShaderResourceView(data->m_texture, &srv, &data->m_srv);

    if (isDepth) {
        D3D11_DEPTH_STENCIL_VIEW_DESC dsv = {};
        dsv.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        this->m_d3dDevice->CreateDepthStencilView(data->m_texture, &dsv, &data->m_dsv);
    } else if (texId->m_flags.m_renderTarget) {
        for (uint32_t face = 0; face < faces; face++) {
            D3D11_RENDER_TARGET_VIEW_DESC rtv = {};
            rtv.Format = desc.Format;

            if (isCube) {
                rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
                rtv.Texture2DArray.FirstArraySlice = face;
                rtv.Texture2DArray.ArraySize = 1;
            } else {
                rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            }

            this->m_d3dDevice->CreateRenderTargetView(data->m_texture, &rtv, &data->m_rtv[face]);
        }
    }

    texId->m_apiSpecificData = data;
    texId->m_needsCreation = 0;
    return 1;
}

static void TexRowInfo(EGxTexFormat format, uint32_t width, uint32_t height, uint32_t& pitch, uint32_t& rows) {
    if (format >= GxTex_Dxt1 && format <= GxTex_Dxt5) {
        pitch = CGxDevice::s_texFormatBytesPerBlock[format] * ((width + 3) >> 2);
        rows = (height + 3) >> 2;
    } else {
        pitch = (width * CGxDevice::s_texFormatBitDepth[format]) >> 3;
        rows = height;
    }
}

void CGxDeviceD3d11::ITexUpload(CGxTex* texId) {
    uint32_t texelStrideInBytes;
    const void* texels = nullptr;
    texId->m_userFunc(GxTex_Lock, texId->m_width, texId->m_height, 0, 0, texId->m_userArg, texelStrideInBytes, texels);

    uint32_t width;
    uint32_t height;
    uint32_t baseMip;
    uint32_t mipCount;
    this->ITexWHDStartEnd(texId, width, height, baseMip, mipCount);

    TexData* data = static_cast<TexData*>(texId->m_apiSpecificData);
    int32_t compressed = texId->m_dataFormat >= GxTex_Dxt1 && texId->m_dataFormat <= GxTex_Dxt5;
    uint32_t faceCount = texId->m_target == GxTex_CubeMap ? 6 : 1;
    std::vector<uint8_t> scratch;

    for (uint32_t face = 0; face < faceCount && data && data->m_texture; face++) {
        uint32_t mipWidth = width;
        uint32_t mipHeight = height;
        uint32_t level = 0;

        for (uint32_t mip = baseMip; mip != mipCount; mip++, level++) {
            texels = nullptr;
            texId->m_userFunc(GxTex_Latch, mipWidth, mipHeight, face, mip, texId->m_userArg, texelStrideInBytes, texels);

            static int32_t s_uploadLogs = 0;

            if (width >= 512 && s_uploadLogs < 60) {
                s_uploadLogs++;
                D3d11Log("upload %ux%u fmt %d dataFmt %d mip %u/%u level %u: texels %p stride %u first %02x%02x%02x%02x rect %d,%d,%d,%d", width, height, texId->m_format, texId->m_dataFormat, mip, mipCount, level, texels, texelStrideInBytes, texels ? static_cast<const uint8_t*>(texels)[0] : 0, texels ? static_cast<const uint8_t*>(texels)[1] : 0, texels ? static_cast<const uint8_t*>(texels)[2] : 0, texels ? static_cast<const uint8_t*>(texels)[3] : 0, texId->m_updateRect.minX, texId->m_updateRect.minY, texId->m_updateRect.maxX, texId->m_updateRect.maxY);
            }

            if (texels && !texId->m_flags.m_renderTarget && level < data->m_mips) {
                RECT rect;
                rect.left = texId->m_updateRect.minX >> mip;
                rect.top = texId->m_updateRect.minY >> mip;
                rect.bottom = rect.top + 1 > texId->m_updateRect.maxY >> mip ? rect.top + 1 : texId->m_updateRect.maxY >> mip;
                rect.right = rect.left + 1 > texId->m_updateRect.maxX >> mip ? rect.left + 1 : texId->m_updateRect.maxX >> mip;

                if (static_cast<uint32_t>(texId->m_format - GxTex_Dxt1) <= 2) {
                    rect.left &= ~3;
                    rect.top &= ~3;
                    rect.right = (rect.right + 3) & ~3;
                    rect.bottom = (rect.bottom + 3) & ~3;

                    if (rect.bottom > static_cast<int32_t>(mipHeight)) {
                        rect.bottom = mipHeight;
                    }

                    if (rect.right > static_cast<int32_t>(mipWidth)) {
                        rect.right = mipWidth;
                    }
                }

                if (rect.right > static_cast<int32_t>(mipWidth)) {
                    rect.right = mipWidth;
                }

                if (rect.bottom > static_cast<int32_t>(mipHeight)) {
                    rect.bottom = mipHeight;
                }

                if (rect.left < rect.right && rect.top < rect.bottom) {
                    const uint8_t* src = static_cast<const uint8_t*>(texels);

                    if (!texId->m_flags.m_bit15) {
                        EGxTexFormat dataFormat = texId->m_dataFormat;

                        if (compressed) {
                            src += s_texFormatBytesPerBlock[dataFormat] * (rect.left >> 2) + texelStrideInBytes * (rect.top >> 2);
                        } else {
                            src += ((rect.left * s_texFormatBitDepth[dataFormat]) >> 3) + texelStrideInBytes * rect.top;
                        }
                    }

                    C2iVector size;
                    size.x = rect.right - rect.left;
                    size.y = rect.bottom - rect.top;

                    uint32_t pitch;
                    uint32_t rows;
                    TexRowInfo(texId->m_format, size.x, size.y, pitch, rows);
                    scratch.resize(static_cast<size_t>(pitch) * rows);

                    Blit(size, BlitAlpha_0, src, texelStrideInBytes, GxGetBlitFormat(texId->m_dataFormat), scratch.data(), pitch, GxGetBlitFormat(texId->m_format));

                    D3D11_BOX box = { static_cast<UINT>(rect.left), static_cast<UINT>(rect.top), 0, static_cast<UINT>(rect.right), static_cast<UINT>(rect.bottom), 1 };
                    this->m_d3dContext->UpdateSubresource(data->m_texture, D3D11CalcSubresource(level, face, data->m_mips), &box, scratch.data(), pitch, 0);
                }
            }

            mipHeight >>= 1;
            mipWidth >>= 1;

            if (!mipWidth) {
                mipWidth = 1;
            }

            if (!mipHeight) {
                mipHeight = 1;
            }
        }
    }

    texels = nullptr;
    texId->m_userFunc(GxTex_Unlock, texId->m_width, texId->m_height, 0, 0, texId->m_userArg, texelStrideInBytes, texels);
}

void CGxDeviceD3d11::ITexMarkAsUpdated(CGxTex* texId) {
    if (!texId->m_needsUpdate || !this->m_context) {
        return;
    }

    if (texId->m_needsCreation || !texId->m_apiSpecificData) {
        this->ITexCreate(texId);
    }

    if (!texId->m_needsCreation && texId->m_apiSpecificData) {
        if (texId->m_userFunc) {
            this->ITexUpload(texId);
        }

        CGxDevice::ITexMarkAsUpdated(texId);
    }
}

void CGxDeviceD3d11::ITexForceRecreation(int32_t all) {
    for (CGxTex* texId = this->m_textures.Head(); texId; texId = this->m_textures.Next(texId)) {
        if (!texId->m_apiSpecificData || !(all || texId->m_flags.m_renderTarget)) {
            continue;
        }

        static CiRect s_updateRect;

        this->ITexReleaseData(texId);
        texId->m_needsCreation = 1;
        this->TexMarkForUpdate(texId, s_updateRect, 0);

        uint32_t texelStrideInBytes;
        const void* texels;
        texId->m_userFunc(GxTex_3, texId->m_width, texId->m_height, 0, 0, texId->m_userArg, texelStrideInBytes, texels);
    }

    if (this->m_hwnd && this->m_d3dDevice) {
        this->NotifyOnTextureRecreation();
    }
}

// ---------------------------------------------------------------------------------------------
// shaders

static uint64_t Fnv(const void* data, size_t size, uint64_t hash = 1469598103934665603ull) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);

    for (size_t i = 0; i < size; i++) {
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    }

    return hash;
}

CGxDeviceD3d11::ShaderData* CGxDeviceD3d11::ICompileShader(const std::string& hlsl, const char* target, const D3d9ShaderInfo& info, const char* name) {
    ID3DBlob* blob = nullptr;
    ID3DBlob* errors = nullptr;
    HRESULT hr = D3DCompile(hlsl.data(), hlsl.size(), name, nullptr, nullptr, "main", target, D3DCOMPILE_OPTIMIZATION_LEVEL1, 0, &blob, &errors);

    if (FAILED(hr)) {
        D3d11Log("CGxDeviceD3d11: shader '%s' (%s) failed to compile: %s", name, target, errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
        Rel(errors);
        Rel(blob);

        char path[MAX_PATH];
        snprintf(path, sizeof(path), "Logs\\d3d11_fail_%s.hlsl", name);

        if (FILE* file = fopen(path, "wb")) {
            fwrite(hlsl.data(), 1, hlsl.size(), file);
            fclose(file);
        }

        return nullptr;
    }

    Rel(errors);

    ShaderData* data = new ShaderData();
    snprintf(data->m_name, sizeof(data->m_name), "%s", name);
    data->m_blob = blob;

    if (GetEnvironmentVariableA("WOW_D3D11_DUMPHLSL", nullptr, 0)) {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "Logs\\d3d11_ok_%s.hlsl", name);

        if (FILE* file = fopen(path, "wb")) {
            fwrite(hlsl.data(), 1, hlsl.size(), file);
            fclose(file);
        }
    }
    data->m_inputs = info.m_inputs;

    if (info.m_isPixel) {
        ID3D11PixelShader* shader = nullptr;
        hr = this->m_d3dDevice->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &shader);
        data->m_shader = shader;
    } else {
        ID3D11VertexShader* shader = nullptr;
        hr = this->m_d3dDevice->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &shader);
        data->m_shader = shader;
    }

    if (FAILED(hr) || !data->m_shader) {
        Rel(data->m_blob);
        delete data;
        return nullptr;
    }

    return data;
}

void CGxDeviceD3d11::IShaderCreateFor(CGxShader* shader) {
    shader->m_valid = 0;
    shader->m_apiSpecific = nullptr;

    if (!this->m_context) {
        return;
    }

    shader->m_loaded = 1;

    if (!shader->m_code.Count() || (shader->m_target != GxSh_Vertex && shader->m_target != GxSh_Pixel)) {
        D3d11Log("CGxDeviceD3d11: shader '%s' (%s) has no bytecode", shader->m_key.m_str ? shader->m_key.m_str : "?", shader->m_target == GxSh_Vertex ? "vs" : "ps");
        return;
    }

    const uint8_t* code = shader->m_code.m_data;
    uint32_t size = shader->m_code.Count();

    uint64_t hash = Fnv(code, size, shader->m_target == GxSh_Pixel ? 0x9E3779B97F4A7C15ull : 0xC2B2AE3D27D4EB4Full);
    char key[40];
    snprintf(key, sizeof(key), "%016llx%08x", static_cast<unsigned long long>(hash), size);

    auto found = this->m_shaderCache.find(key);

    if (found != this->m_shaderCache.end()) {
        shader->m_apiSpecific = found->second;
        shader->m_valid = found->second != nullptr;
        return;
    }

    std::string hlsl;
    D3d9ShaderInfo info;
    ShaderData* data = nullptr;

    if (D3d9ShaderToHlsl(code, size, hlsl, info)) {
        data = this->ICompileShader(hlsl, info.m_isPixel ? "ps_5_0" : "vs_5_0", info, key);

        if (data) {
            snprintf(data->m_name, sizeof(data->m_name), "%s@%.8s", shader->m_key.m_str ? shader->m_key.m_str : "?", key);
        }

        D3d11Log("CGxDeviceD3d11: shader '%s' (%s, %u bytes, sm %u.%u) -> %s %s", shader->m_key.m_str ? shader->m_key.m_str : "?", info.m_isPixel ? "ps" : "vs", size, info.m_major, info.m_minor, key, data ? "ok" : "FAILED");
    } else {
        D3d11Log("CGxDeviceD3d11: unsupported shader '%s' %s (opcode %u)", shader->m_key.m_str ? shader->m_key.m_str : "?", key, info.m_failOp);
    }

    this->m_shaderCache[key] = data;
    shader->m_apiSpecific = data;
    shader->m_valid = data != nullptr;
}

void CGxDeviceD3d11::IShaderCreate(CGxShader* shader) {
    this->IShaderCreateFor(shader);
}

void CGxDeviceD3d11::IReleaseShaderData(CGxShader* shader) {
    // ShaderData is owned by the cache and shared between identical shaders
    shader->m_apiSpecific = nullptr;
    shader->m_loaded = 0;
    shader->m_valid = 0;
}

void CGxDeviceD3d11::ShaderCreate(CGxShader** shaders, EGxShTarget target, const char* path, const char* name, uint32_t permutations) {
    CGxDevice::ShaderCreate(shaders, target, path, name, permutations);

    if (permutations == 1 && !shaders[0]->m_loaded) {
        this->IShaderCreateFor(shaders[0]);
    }
}

void CGxDeviceD3d11::ShaderDestroy(CGxShader*& shader) {
    if (shader->m_refCount == 1) {
        this->IReleaseShaderData(shader);
    }

    CGxDevice::ShaderDestroy(shader);
}

void CGxDeviceD3d11::IShaderReload(CGxShader* shader, const char* path, const char* name) {
    if (shader->m_target != GxSh_Vertex && shader->m_target != GxSh_Pixel) {
        return;
    }

    if (shader->m_refCount == 1) {
        this->IReleaseShaderData(shader);
    }

    this->IShaderLoad(&shader, static_cast<EGxShTarget>(shader->m_target), path, name, 1);
    this->IShaderCreateFor(shader);
}

void CGxDeviceD3d11::IReleaseShaders() {
    for (int32_t target = 0; target < GxShTargets_Last; target++) {
        TSHashTable<CGxShader, HASHKEY_STRI>& list = this->m_shaderList[target];

        for (CGxShader* shader = list.m_fulllist.Head(); shader; shader = list.m_fulllist.Next(shader)) {
            this->IReleaseShaderData(shader);
        }
    }

    for (auto& entry : this->m_shaderCache) {
        ShaderData* data = entry.second;

        if (data) {
            Rel(data->m_shader);
            Rel(data->m_blob);
            delete data;
        }
    }

    this->m_shaderCache.clear();
}

void CGxDeviceD3d11::IReleaseResources(int32_t freeTextures) {
    this->ITexForceRecreation(freeTextures);
    this->IReleaseShaders();

    for (CGxPool* pool = this->m_poolList.Head(); pool; pool = this->m_poolList.Next(pool)) {
        this->IReleasePool(pool);
    }

    for (CGxQuery* query = this->m_queryList.Head(); query; query = this->m_queryList.Next(query)) {
        if (query->m_apiSpecific) {
            static_cast<ID3D11Query*>(query->m_apiSpecific)->Release();
        }

        query->m_apiSpecific = nullptr;
    }
}

void CGxDeviceD3d11::IReleaseStateCaches() {
    for (auto& e : this->m_blendStates) {
        e.second->Release();
    }

    for (auto& e : this->m_depthStates) {
        e.second->Release();
    }

    for (auto& e : this->m_rasterStates) {
        e.second->Release();
    }

    for (auto& e : this->m_samplerStates) {
        e.second->Release();
    }

    for (auto& e : this->m_inputLayouts) {
        if (e.second) {
            e.second->Release();
        }
    }

    this->m_blendStates.clear();
    this->m_depthStates.clear();
    this->m_rasterStates.clear();
    this->m_samplerStates.clear();
    this->m_inputLayouts.clear();
}
