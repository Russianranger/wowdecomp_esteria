#pragma once
#include <cstdint>
#include <memory>
#include <stdexcept>
class CGxDevice;
// Host shim: the ASEC host saved/restored device state and isolated stream buffers around module draws.
// shortcut: no-op scope, so M3 draws share this host's Gx state; port real isolation if state leaks into M2 draws.
namespace Gx {
struct PrivateStreamBuffers {};
class RenderScope {
public:
    using DiagnosticFn = void (*)(const char*);
    RenderScope(CGxDevice*, PrivateStreamBuffers*, uint64_t& checks, uint64_t&, DiagnosticFn) { ++checks; }
    bool Ready() const { return true; }
};
}
