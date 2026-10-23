#pragma once
#include "ASECM3/DDS.h"
#include "Engine/Source/Gx/Types.h"
#include <memory>
class CGxDevice;
class CGxTex;
namespace ASECM3::Client {
// Render-thread-owned texture. The callback's address and every source byte
// remain stable until destruction. Destroy before deleting the owning device;
// ordinary device reset/recreation can reuse the retained DDS payload.
class GxDDS final {
    DDSImage m_image;
    CGxDevice* m_device = nullptr;
    CGxTex* m_texture = nullptr;
    uint32_t m_thread = 0;
    bool m_badRequest = false;
    uint64_t m_uploadLatches = 0;
    uint32_t m_uploadedFaces=0;
    std::vector<const DDSMip*> m_subresources;
    static void Supply(EGxTexCommand command,uint32_t width,uint32_t height,uint32_t face,uint32_t level,
                       void* owner,uint32_t& stride,const void*& pixels) noexcept;
    GxDDS(DDSImage image,const std::string& name,bool wrapU,bool wrapV);
public:
    static std::unique_ptr<GxDDS> Create(DDSImage image,const std::string& name,bool wrapU,bool wrapV);
    ~GxDDS();
    GxDDS(const GxDDS&)=delete;
    GxDDS& operator=(const GxDDS&)=delete;
    GxDDS(GxDDS&&)=delete;
    GxDDS& operator=(GxDDS&&)=delete;
    CGxTex* Get() const;
    void SetWrap(bool wrapU,bool wrapV);
    bool Uploaded() const noexcept;
    uint64_t UploadLatches() const { return m_uploadLatches; }
    uint32_t UploadedFaces() const { return m_uploadedFaces; }
    const DDSImage& Image() const { return m_image; }
    bool InvalidUploadRequest() const { return m_badRequest; }
};
}
