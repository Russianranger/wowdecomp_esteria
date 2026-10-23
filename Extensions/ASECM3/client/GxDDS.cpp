#include "GxDDS.h"
#include <Windows.h>
#include <limits>
#include <utility>
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxTex.h"
#include "Engine/Source/Gx/CGxTexFlags.h"
namespace ASECM3::Client {
namespace {
EGxTexFormat Format(DDSFormat format) {
    switch (format) {
    case DDSFormat::BC1:return GxTex_Dxt1;
    case DDSFormat::BC2:return GxTex_Dxt3;
    case DDSFormat::BC3:return GxTex_Dxt5;
    case DDSFormat::RGBA8:return GxTex_Abgr8888;
    case DDSFormat::BGRA8:return GxTex_Argb8888;
    default:throw Error(std::string("DDS format requires a newer graphics adapter: ")+DDSFormatName(format));
    }
}
uint32_t EngineLevels(const DDSImage& image) {
    const bool compressed=image.format==DDSFormat::BC1 || image.format==DDSFormat::BC2 || image.format==DDSFormat::BC3;
    uint32_t extent=compressed ? (image.width<image.height?image.width:image.height) : (image.width>image.height?image.width:image.height);
    uint32_t count=1;
    while (extent>(compressed?4u:1u)) { extent>>=1; ++count; }
    return count;
}
}
std::unique_ptr<GxDDS> GxDDS::Create(DDSImage image,const std::string& name,bool wrapU,bool wrapV) {
    return std::unique_ptr<GxDDS>(new GxDDS(std::move(image),name,wrapU,wrapV));
}
GxDDS::GxDDS(DDSImage image,const std::string& name,bool wrapU,bool wrapV):m_image(std::move(image)) {
    if (!g_theGxDevicePtr) throw Error("No graphics device for DDS upload");
    if (!m_image.width || !m_image.height || !m_image.levels || (m_image.faces!=1 && m_image.faces!=6) ||
        uint64_t(m_image.faces)*m_image.levels!=m_image.mips.size()) throw Error("Invalid DDS upload dimensions");
    if (m_image.levels>32) throw Error("DDS upload mip count exceeds native range");
    const auto format=Format(m_image.format);
    m_subresources.resize(m_image.mips.size(),nullptr);
    for (const auto& mip:m_image.mips) {
        if (mip.face>=m_image.faces || mip.level>=m_image.levels || mip.rowBytes>std::numeric_limits<uint32_t>::max())
            throw Error("DDS upload subresource is invalid");
        const size_t index=size_t(mip.face)*m_image.levels+mip.level;
        if (m_subresources[index]) throw Error("Duplicate DDS upload subresource");
        if (mip.width!=(m_image.width>>mip.level ? m_image.width>>mip.level : 1) ||
            mip.height!=(m_image.height>>mip.level ? m_image.height>>mip.level : 1)) throw Error("DDS upload mip dimensions disagree");
        const bool compressed=m_image.format==DDSFormat::BC1 || m_image.format==DDSFormat::BC2 || m_image.format==DDSFormat::BC3;
        const size_t row=compressed ? size_t((mip.width+3)/4)*(m_image.format==DDSFormat::BC1?8:16) : size_t(mip.width)*4;
        const size_t rows=compressed ? (mip.height+3)/4 : mip.height;
        if (mip.rowBytes!=row || mip.size!=row*rows) throw Error("DDS upload pitch/payload size disagree");
        m_image.Pixels(mip); // validate each retained byte range before publishing callback
        m_subresources[index]=&mip;
    }
    m_device=g_theGxDevicePtr; m_thread=GetCurrentThreadId();
    // The 335 engine chooses mip counts itself. A partial chain must not leave
    // uninitialized GPU mip levels available to the minification sampler.
    const bool chain=m_image.levels>=EngineLevels(m_image);
    CGxTexFlags flags(chain?GxTex_LinearMipLinear:GxTex_Linear,wrapU,wrapV,0,0,0,1,0,0,0);
    const auto target=m_image.faces==6?GxTex_CubeMap:GxTex_2d;
    if (!m_device->TexCreate(target,m_image.width,m_image.height,1,format,format,flags,this,&Supply,name.c_str(),m_texture) || !m_texture)
        throw Error("Could not create native DDS texture");
}
GxDDS::~GxDDS() {
    if (m_texture && m_device) m_device->TexDestroy(m_texture);
}
CGxTex* GxDDS::Get() const {
    if (GetCurrentThreadId()!=m_thread || g_theGxDevicePtr!=m_device) throw Error("DDS texture used outside its owning render context");
    if (m_badRequest) throw Error("Graphics device requested an invalid DDS subresource");
    return m_texture;
}
void GxDDS::SetWrap(bool wrapU,bool wrapV) {
    auto* texture=Get();
    const bool chain=m_image.levels>=EngineLevels(m_image);
    m_device->TexSetFlags(texture,CGxTexFlags(chain?GxTex_LinearMipLinear:GxTex_Linear,wrapU,wrapV,0,0,0,1,0,0,0));
}
bool GxDDS::Uploaded() const noexcept {
    if(GetCurrentThreadId()!=m_thread || g_theGxDevicePtr!=m_device || m_badRequest || !m_texture)return false;
    const auto* texture=m_texture;
    return !texture->m_needsCreation && !texture->m_needsUpdate &&
        (texture->m_apiSpecificData || texture->m_apiSpecificData2) && m_uploadLatches>0;
}
void GxDDS::Supply(EGxTexCommand command,uint32_t width,uint32_t height,uint32_t face,uint32_t level,
                   void* owner,uint32_t& stride,const void*& pixels) noexcept {
    stride=0; pixels=nullptr;
    if (command!=GxTex_Latch || !owner) return;
    auto& texture=*static_cast<GxDDS*>(owner);
    if (face>=texture.m_image.faces || level>=texture.m_image.levels) { texture.m_badRequest=true; return; }
    const auto* mip=texture.m_subresources[size_t(face)*texture.m_image.levels+level];
    if (!mip || mip->width!=width || mip->height!=height) { texture.m_badRequest=true; return; }
    try {
        pixels=texture.m_image.Pixels(*mip); stride=static_cast<uint32_t>(mip->rowBytes);++texture.m_uploadLatches;texture.m_uploadedFaces|=1u<<face;
    } catch (...) { texture.m_badRequest=true; pixels=nullptr; stride=0; }
}
}
