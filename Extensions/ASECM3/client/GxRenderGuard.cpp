#include "GxRenderGuard.h"
#include "Engine/Source/lua-5.1/src/asecDiagnostics.h"
namespace ASECM3::Client {
namespace {
void Diagnostic(const char* error){ASEC_LuaDiagnostic(nullptr,error,0,0,0,__FILE__,__LINE__);}
}
GxRenderGuard::GxRenderGuard(CGxDevice* device,GxStreamBuffers& buffers,bool enabled,uint64_t& checks,uint64_t& failures){
    if(enabled)try{state=std::make_unique<Gx::RenderScope>(device,&buffers,checks,failures,Diagnostic);}
    catch(const std::exception& e){ready=false;++failures;Diagnostic(e.what());}
}
GxRenderGuard::~GxRenderGuard()=default;
}
