#pragma once
#include "Engine/Source/Gx/GxRenderScope.h"
namespace ASECM3::Client {
using GxStreamBuffers=Gx::PrivateStreamBuffers;
// M3 retains its caller contract and Lua diagnostics around the shared Gx scope.
class GxRenderGuard {
    std::unique_ptr<Gx::RenderScope> state;
    bool ready=true;
public:
    GxRenderGuard(CGxDevice* device,GxStreamBuffers& buffers,bool enabled,uint64_t& checks,uint64_t& failures);
    ~GxRenderGuard();
    bool Ready() const {return ready && (!state || state->Ready());}
};
}
